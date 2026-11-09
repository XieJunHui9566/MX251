"""Check M161 frames from wide-many-arguments-hardware.c."""
import argparse
import hashlib
import json
from pathlib import Path


def expected(x):
    def signed8(value):
        value &= 255
        return value - 256 if value & 128 else value

    terms = [x << 1, (0x1234 + x) << 3, signed8(x ^ 0xa5),
             ((0x8123 + x) & 65535) << 7, 0x12345678 + x,
             ((x + 17) & 255) << 19, (0xfedc - x) << 11,
             (0x89abcdef ^ x) >> 5, [0x12, 0x96, 0xa5, 0xef][x & 3] << 24,
             ((x * 3) & 255) << 9, (0x4567 ^ x) << 15, 0xdeadbeef - x,
             [0x33, 0x7f, 0xc0, 0x5a][x & 3] << 17,
             signed8(x + 93) << 13, (0xabcd + x) << 2,
             (0x76543210 ^ x) >> 3]
    result = x
    for term in terms:
        result ^= term
    return result & 0xffffffff


def check(data):
    offset = data.find(b'M161')
    assert offset >= 0, 'no M161 frames'
    seen, frames = set(), 0
    while offset + 20 <= len(data):
        frame = data[offset:offset + 20]
        assert frame[:4] == b'M161' and frame[-2:] == b'\r\n', offset
        checksum = 0
        for byte in frame[:18]:
            checksum ^= byte
        assert checksum == 0, (offset, 'checksum')
        x = frame[4]
        actual = [int.from_bytes(frame[k:k + 4], 'big') for k in (5, 9, 13)]
        assert actual == [expected(x)] * 3, (x, actual, expected(x))
        seen.add(x)
        frames += 1
        offset += 20
    assert len(seen) == 256, len(seen)
    return dict(status='PASS', frames=frames, inputCases=256, callsPerCase=3,
                parametersPerCall=16, uartSha256=hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path)
    a = p.parse_args()
    print(json.dumps(check(a.capture.read_bytes()), indent=2))
