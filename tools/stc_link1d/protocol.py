"""STC-Link1D protocol inferred from one successful AiCube-ISP capture.

Scope: 34BF:1005, captured Link1D firmware, AI8051U-34K64 status 74/55/0B,
40 MHz IRC, FF0000 application page. No configuration-only operation.
"""
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import time


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class Firmware:
    code: bytes
    sha256: str
    populated_bytes: int

    @classmethod
    def load(cls, path):
        raw = Path(path).read_bytes()
        return cls.parse(raw)

    @classmethod
    def parse(cls, raw):
        memory = {}
        high = 0
        ended = False
        for line in raw.decode("ascii").splitlines():
            line = line.strip()
            if not line:
                continue
            if ended or not line.startswith(":"):
                raise ProtocolError("Missing colon or data after HEX EOF")
            try:
                r = bytes.fromhex(line[1:])
            except ValueError as error:
                raise ProtocolError("Invalid HEX encoding") from error
            if len(r) < 5 or len(r) != r[0] + 5 or sum(r) & 255:
                raise ProtocolError("HEX length/checksum mismatch")
            count, address, kind = r[0], int.from_bytes(r[1:3], "big"), r[3]
            data = r[4:-1]
            if kind == 0:
                if address + count > 65536:
                    raise ProtocolError("HEX record crosses a segment")
                for i, value in enumerate(data):
                    absolute = high + address + i
                    if not 0xFF0000 <= absolute <= 0xFFFFFF or absolute in memory:
                        raise ProtocolError("HEX overlap or address outside FF0000..FFFFFF")
                    memory[absolute] = value
            elif kind == 1:
                if count or address:
                    raise ProtocolError("Invalid HEX EOF")
                ended = True
            elif kind in (2, 4):
                if count != 2 or address:
                    raise ProtocolError("Invalid HEX extended address")
                high = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
            else:
                raise ProtocolError(f"Unsupported HEX record type {kind}")
        if not ended or not memory or min(memory) != 0xFF0000:
            raise ProtocolError("HEX requires EOF and reset entry at FF0000")
        code = bytes(memory.get(a, 255) for a in range(0xFF0000, max(memory) + 1))
        return cls(code, hashlib.sha256(raw).hexdigest(), len(memory))


def encode(command, data=b"", offset=0):
    if not 0 <= command <= 255 or len(data) > 56 or not 0 <= offset <= 0xFFFFFFFF:
        raise ProtocolError("Invalid bridge command/fragment")
    report = bytearray(65)
    report[1:3] = bytes((0x5A, command))
    report[3:7] = offset.to_bytes(4, "big")
    report[7] = len(data)
    report[8:8 + len(data)] = data
    report[64] = -sum(report[:64]) & 255
    return bytes(report)


def decode(report):
    if len(report) != 65 or report[:2] != b"\x00\xa5" or sum(report) & 255:
        raise ProtocolError("HID reply size/header/checksum mismatch")
    size = int.from_bytes(report[2:4], "big")
    if size > 60:
        raise ProtocolError("Oversized HID reply payload")
    return report[4:4 + size]


# Identification prefix excludes the device-specific eight-byte suffix.
LINK_ID_PREFIX = bytes.fromhex("510119ff130100000000ffffffff20600000")
# Observed post-close control report. Its fields are deliberately not named:
# it is not a normal length-prefixed command and has no observed reply.
POST_SESSION_REPORT = bytes.fromhex("005a400000000000030f54") + bytes(54)
COARSE = bytes.fromhex("00080000ff000010ff100020ff200030ff30")
OPTIONS = bytes.fromhex("0400005aa5ffffff00ffff00ffffffffffffffffffffffffffffff00ff0262b180823001ffffff3e3fbfaff7fefe")


class Link1D:
    def __init__(self, transport, event=lambda _: None, sleep=time.sleep):
        self.transport, self.event, self.sleep = transport, event, sleep
        self.state = "new"
        self.erase_attempted = False
        self.options_attempted = False

    def send(self, command, data=b"", offset=0):
        self.transport.write(encode(command, data, offset))

    def exchange(self, command, data, prefix, size, timeout=3000):
        self.send(command, data)
        reply = decode(self.transport.read(timeout))
        if len(reply) != size or not reply.startswith(prefix):
            raise ProtocolError(f"Unexpected reply to {command:02X}: {reply.hex()}")
        return reply

    def control(self, data, delay):
        self.send(0x40, bytes.fromhex(data))
        self.sleep(delay)

    def detect(self):
        if self.state != "new":
            raise ProtocolError("Session cannot be reused")
        identity = self.exchange(0, b"", LINK_ID_PREFIX, 26)
        self.event({"stage": "bridge-identified", "identity": identity.hex()})
        for data, delay in (("0a01", .105), ("020201", .065), ("0400", .002), ("0328", .002)):
            self.control(data, delay)
        status = self.exchange(0x41, bytes.fromhex("2800030500090000"), b"\x50", 52, 5000)
        if status[20:22] != b"\x78\xb4" or bytes((status[17], status[18], status[22])) != b"\x74\x55\x0b":
            raise ProtocolError("Target/BSL differs from captured AI8051U-34K64 profile")
        # Preserve the captured hardware-option contract. FC -> FE at byte 16
        # occurred in the successful official download; accept the resulting FE.
        expected = {8: 0xAF, 9: 0xFF, 10: 0xBF, 11: 0x3E, 12: 0x3F, 15: 0xF7, 51: 0xFE}
        if any(status[k] != v for k, v in expected.items()) or status[16] not in (0xFC, 0xFE):
            raise ProtocolError("Hardware options differ from captured profile; no erase sent")
        self.state = "identified"
        self.event({"stage": self.state, "model": "AI8051U-34K64", "rawStatus": status.hex()})
        return status

    def calibrate(self):
        if self.state != "identified":
            raise ProtocolError("Calibration requires identification")
        reply = self.exchange(0x43, COARSE, b"\x00\x08", 18)
        wanted = 40000000 / 2400
        selected = None
        for i in range(4):
            low = int.from_bytes(reply[2 + 4 * i:4 + 4 * i], "big")
            high = int.from_bytes(reply[4 + 4 * i:6 + 4 * i], "big")
            if low == 0 or high <= low:
                raise ProtocolError("Invalid calibration range")
            if selected is None and low <= wanted <= high:
                selected = (int((wanted - low) * 255 / (high - low)), i * 16)
        if selected is None:
            raise ProtocolError("40 MHz is outside measured IRC ranges")
        center, band = selected
        start = max(0, min(244, center - 6))
        fine = bytes((0, 12)) + b"".join(bytes((start + i, band)) for i in range(12))
        reply = self.exchange(0x43, fine, b"\x00\x0c", 26)
        counts = [int.from_bytes(reply[2 + 2 * i:4 + 2 * i], "big") for i in range(12)]
        if any(c == 0 for c in counts) or any(a > b for a, b in zip(counts, counts[1:])):
            raise ProtocolError("Invalid fine calibration counts")
        best = min(range(12), key=lambda i: abs(counts[i] - wanted))
        if abs(counts[best] - wanted) / wanted > .005:
            raise ProtocolError("IRC error exceeds 0.5%; no erase sent")
        self.calibration = (counts[best] * 2400, start + best, band)
        self.state = "calibrated"
        self.event({"stage": self.state, "requestedHz": 40000000, "measuredHz": self.calibration[0]})

    def prepare(self):
        if self.state != "calibrated":
            raise ProtocolError("Programming requires fresh calibration")
        _, trim, band = self.calibration
        self.exchange(0x43, bytes((1, 0, 0, 0xFF, 0xCC, band, trim, 0x98)), b"\x01", 1)
        self.control("026001", .100)
        self.exchange(0x43, bytes.fromhex("0500005aa5"), b"\x05", 1)
        self.state = "prepared"

    def program(self, code):
        if self.state != "prepared" or not 1 <= len(code) <= 65536:
            raise ProtocolError("Unprepared session or invalid image size")
        self.state = "programming"
        self.erase_attempted = True
        self.exchange(0x43, bytes.fromhex("0300005aa5"), b"\x03\x78\xb4", 8, 5000)
        padded = code + b"\xff" * (-len(code) % 64)
        for address in range(0, len(padded), 64):
            block = bytes((0x22 if address == 0 else 2,)) + address.to_bytes(2, "big") + b"\x5a\xa5" + padded[address:address + 64]
            self.send(0x44, block[:56])
            self.send(0x44, block[56:], 56)
            self.exchange(0x45, b"\x45", b"\x02\x54", 2)
            self.event({"stage": "program", "acknowledgedBytes": address + 64, "totalBytes": len(padded)})
            self.sleep(.020)
        self.state = "image-acknowledged"

    def configure(self):
        if self.state != "image-acknowledged":
            raise ProtocolError("Configuration requires every code block ACK")
        options = bytearray(OPTIONS)
        hz, trim, band = self.calibration
        options[29:33] = hz.to_bytes(4, "big")
        options[33:36] = bytes((trim, band, 1))
        self.sleep(.060)
        self.options_attempted = True
        self.state = "configuring"
        self.exchange(0x43, options, b"\x04\x54", 2)
        self.state = "options-acknowledged"

    def run(self):
        if self.state != "options-acknowledged":
            raise ProtocolError("Run requires complete firmware and configuration ACKs")
        for data, delay in (("0401", .110), ("0c01", .002), ("00", .520), ("0328", .002), ("0a00", .002)):
            self.control(data, delay)
        self.send(0xFF)
        self.state = "run-sent"

    def flash(self, code):
        if not 1 <= len(code) <= 65536:
            raise ProtocolError("Code must fit 64 KiB")
        try:
            self.detect()
            self.calibrate()
            self.prepare()
            self.program(code)
            self.configure()
            self.run()
        except BaseException:
            self.state = "failed"
            self.event({"stage": "failed", "eraseAttempted": self.erase_attempted,
                        "configurationAttempted": self.options_attempted, "automaticRetry": False})
            raise
        return {"programAcknowledged": True, "configurationAcknowledged": True,
                "flashReadback": False, "configurationReadback": False,
                "runCommandSent": True, "programExecutionVerified": False,
                "imageBytes": len(code), "programBlocks": math.ceil(len(code) / 64)}
