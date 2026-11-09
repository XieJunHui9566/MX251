"""Offline failure-path checks; never imports the Windows transport."""
import unittest

from protocol import Firmware, Link1D, LINK_ID_PREFIX, ProtocolError, decode, encode


def reply(data):
    r = bytearray(65)
    r[1] = 0xA5
    r[2:4] = len(data).to_bytes(2, "big")
    r[4:4 + len(data)] = data
    r[64] = -sum(r[:64]) & 255
    return bytes(r)


def hex_record(address, kind, data=b""):
    r = bytes((len(data),)) + address.to_bytes(2, "big") + bytes((kind,)) + data
    return ":" + (r + bytes((-sum(r) & 255,))).hex() + "\n"


class Fake:
    def __init__(self):
        status = bytearray(52)
        status[0] = 0x50
        for i, v in {8: 0xAF, 9: 0xFF, 10: 0xBF, 11: 0x3E, 12: 0x3F, 15: 0xF7,
                     16: 0xFC, 17: 0x74, 18: 0x55, 20: 0x78, 21: 0xB4, 22: 0x0B, 51: 0xFE}.items():
            status[i] = v
        self.responses = [LINK_ID_PREFIX + bytes(8), bytes(status),
                          bytes.fromhex("000806da0c6e0b54145d1f13369e2b58548c"),
                          bytes.fromhex("000c40ca40f4412441544160418d41bd41ea42294256428342aa"),
                          b"\x01", b"\x05", b"\x03\x78\xb4" + bytes(5), b"\x02\x54", b"\x04\x54"]
        self.sent = []
        self.read_count = 0
        self.fail_read = None

    def write(self, data):
        self.sent.append(data)

    def read(self, _timeout):
        index = self.read_count
        self.read_count += 1
        if self.fail_read == index:
            raise TimeoutError("Injected timeout")
        return reply(self.responses[index])


class Checks(unittest.TestCase):
    def setup_session(self):
        transport = Fake()
        return transport, Link1D(transport, sleep=lambda _: None)

    def assert_no_program(self, transport):
        self.assertFalse(any(r[2] in (0x44, 0x45) or (r[2] == 0x43 and r[8] in (3, 4)) for r in transport.sent))

    def test_success_padding_and_calibration(self):
        t, s = self.setup_session()
        result = s.flash(bytes(range(63)))
        self.assertTrue(result["programAcknowledged"])
        self.assertFalse(result["flashReadback"])
        fragments = [p for p in t.sent if p[2] == 0x44]
        block = b"".join(p[8:8 + p[7]] for p in fragments)
        self.assertEqual(block, b"\x22\x00\x00\x5a\xa5" + bytes(range(63)) + b"\xff")
        self.assertEqual(s.calibration, (40022400, 130, 48))

    def test_wrong_bridge_fails_before_controls(self):
        t, s = self.setup_session()
        t.responses[0] = bytes(26)
        with self.assertRaises(ProtocolError): s.flash(b"\x00")
        self.assertEqual(len(t.sent), 1)

    def test_wrong_target_and_options_fail_before_erase(self):
        for index in (20, 22, 8):
            with self.subTest(index=index):
                t, s = self.setup_session()
                status = bytearray(t.responses[1]); status[index] ^= 1; t.responses[1] = bytes(status)
                with self.assertRaises(ProtocolError): s.flash(b"\x00")
                self.assert_no_program(t)

    def test_bad_calibration_fails_before_erase(self):
        t, s = self.setup_session()
        t.responses[3] = b"\x00\x0c" + b"\x00\x01" * 12
        with self.assertRaises(ProtocolError): s.flash(b"\x00")
        self.assert_no_program(t)

    def test_block_timeout_never_configures_or_runs_or_retries(self):
        t, s = self.setup_session(); t.fail_read = 7
        with self.assertRaises(TimeoutError): s.flash(b"\x00")
        self.assertFalse(any(r[2] == 0xFF or (r[2] == 0x43 and r[8] == 4) for r in t.sent))
        self.assertEqual(sum(r[2] == 0x45 for r in t.sent), 1)
        self.assertEqual(s.state, "failed")
        with self.assertRaises(ProtocolError): s.run()
        with self.assertRaises(ProtocolError): s.detect()

    def test_bad_block_ack_and_option_ack_do_not_run(self):
        for index in (7, 8):
            t, s = self.setup_session(); t.responses[index] = b"\x00\x54"
            with self.assertRaises(ProtocolError): s.flash(b"\x00")
            self.assertFalse(any(r[2] == 0xFF for r in t.sent))

    def test_no_config_only_or_unprepared_programming(self):
        _, s = self.setup_session()
        for action in (s.configure, s.run, lambda: s.program(b"\x00")):
            with self.assertRaises(ProtocolError): action()

    def test_bad_reply_checksum_and_length(self):
        r = bytearray(reply(b"\x02\x54")); r[5] ^= 1
        with self.assertRaises(ProtocolError): decode(r)
        with self.assertRaises(ProtocolError): decode(bytes(64))
        with self.assertRaises(ProtocolError): encode(0x44, bytes(57))

    def test_hex_bounds_overlap_and_eof(self):
        base = hex_record(0, 4, b"\x00\xff")
        data = hex_record(0, 0, b"\x12\x34")
        end = hex_record(0, 1)
        self.assertEqual(Firmware.parse((base + data + end).encode()).code, b"\x12\x34")
        for invalid in (data + end, base + data + data + end, base + data, base + data + end + data,
                        base + hex_record(65535, 0, b"\x00\x00") + end):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ProtocolError): Firmware.parse(invalid.encode())

    def test_invalid_image_never_opens_protocol(self):
        for code in (b"", bytes(65537)):
            t, s = self.setup_session()
            with self.assertRaises(ProtocolError): s.flash(code)
            self.assertEqual(t.sent, [])


if __name__ == "__main__":
    unittest.main()
