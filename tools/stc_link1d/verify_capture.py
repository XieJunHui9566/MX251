"""Check capture quality and replay the real protocol without opening hardware."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from protocol import Firmware, Link1D, POST_SESSION_REPORT, ProtocolError


def transfers(path):
    rows = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines()]
    counts = Counter(row["kind"] for row in rows)
    if counts["capture_end"] != 1 or counts["stopped"] != 1:
        raise ProtocolError("Capture has not ended cleanly")
    seq = [r["seq"] for r in rows if "seq" in r]
    if seq != list(range(1, len(seq) + 1)):
        raise ProtocolError("Capture sequence gap or duplicate")
    errors = {"agent_error", "capture_warning", "stop_error", "pending_at_close", "io_error", "open_failed"}
    if any(counts[k] for k in errors):
        raise ProtocolError("Capture contains errors; inspect raw trace")
    stopped = next(r for r in rows if r["kind"] == "stopped")
    if stopped["status"]["pending"]:
        raise ProtocolError("Capture ended with pending I/O")
    submitted, completed, requested, received = {}, {}, {}, {}
    stream = []
    for row in rows:
        kind = row["kind"]
        if kind in ("tx_submit", "tx_complete", "rx_request", "rx"):
            table = {"tx_submit": submitted, "tx_complete": completed, "rx_request": requested, "rx": received}[kind]
            if row["id"] in table:
                raise ProtocolError("Duplicate I/O operation ID")
            table[row["id"]] = row
        if kind in ("tx_submit", "rx"):
            packet = bytes.fromhex(row["hex"])
            if len(packet) != 65 or sum(packet) & 255:
                raise ProtocolError("Capture report size/checksum mismatch")
            stream.append(("tx" if kind == "tx_submit" else "rx", packet))
    if submitted.keys() != completed.keys() or requested.keys() != received.keys():
        raise ProtocolError("Incomplete I/O operations")
    for operation, request in submitted.items():
        done = completed[operation]
        if request["requested"] != 65 or done["count"] != 65 or done["seq"] <= request["seq"]:
            raise ProtocolError("Short or out-of-order transmit completion")
    for operation, request in requested.items():
        done = received[operation]
        if request["requested"] != 65 or done["count"] != 65 or done["seq"] <= request["seq"]:
            raise ProtocolError("Short or out-of-order receive completion")
    return stream


class Replay:
    def __init__(self, stream):
        self.stream = stream
        self.position = 0

    def take(self, direction):
        if self.position >= len(self.stream):
            raise ProtocolError("Replay exceeded captured traffic")
        actual, data = self.stream[self.position]
        self.position += 1
        if actual != direction:
            raise ProtocolError(f"Replay direction mismatch at {self.position}: {actual}/{direction}")
        return data

    def write(self, data):
        expected = self.take("tx")
        if data != expected:
            raise ProtocolError(f"Replay TX mismatch at {self.position}: {data.hex()} != {expected.hex()}")

    def read(self, _timeout):
        return self.take("rx")


def verify(capture, firmware):
    stream = transfers(capture)
    transport = Replay(stream)
    session = Link1D(transport, sleep=lambda _: None)
    result = session.flash(firmware.code)
    transport.write(POST_SESSION_REPORT)
    if transport.position != len(stream):
        raise ProtocolError("Replay left unconsumed traffic")
    # These ACK flags describe captured official traffic, not a live script run.
    return {"mode": "offline replay of official capture", "captureSha256": hashlib.sha256(Path(capture).read_bytes()).hexdigest(),
            "hexSha256": firmware.sha256, "reports": len(stream),
            "txReports": sum(d == "tx" for d, _ in stream), "rxReports": sum(d == "rx" for d, _ in stream),
            "allGeneratedReportsMatchCapture": True, "scriptHardwareTested": False, **result}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--capture", type=Path, required=True)
    p.add_argument("--hex", type=Path, required=True)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    outcome = verify(args.capture, Firmware.load(args.hex))
    text = json.dumps(outcome, indent=2)
    if args.output:
        with args.output.open("x", encoding="utf-8") as f:
            f.write(text + "\n")
    print(text)
