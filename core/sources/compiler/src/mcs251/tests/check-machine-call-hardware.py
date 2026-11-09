"""Check captured MCP1 frames against arithmetic independent of target code."""
import argparse
import hashlib
import json
from pathlib import Path


def expected(x):
    y = (x >> 7) ^ 0x96a55a69
    a, b = x & 0xffff, y & 0xffff
    branch = (x + y) & 0xffffffff
    branch = branch ^ y if x & 1 else (branch + x) & 0xffffffff
    loop_x, loop_y = a, b
    for _ in range(3):
        loop_x = (loop_x + loop_y) & 0xffff
        loop_y ^= loop_x
    values = [((a ^ b) + 1) & 0xffff,
              ((x + y) & 0xffffffff) ^ 0x12345678,
              branch ^ x, (loop_x + loop_y) & 0xffff,
              ((a + b) & 0xffff) ^ b]
    return values * 2  # ABI4, then ABI2.


def check(data):
    inputs = [0, 1, 2, 3, 7, 0x80, 0x100, 0xff, 0xffff, 0x10000,
              0x7fffffff, 0x80000000, 0xaaaa5555, 0x5555aaaa,
              0xdeadbeef, 0xffffffff]
    offset = data.find(b'MCP1')
    assert offset >= 0, 'no MCP1 frame'
    seen = set()
    frames = 0
    while offset + 48 <= len(data):
        frame = data[offset:offset + 48]
        assert frame[:4] == b'MCP1' and frame[-2:] == b'\r\n', (offset, frame.hex())
        checksum = 0
        for b in frame[:46]:
            checksum ^= b
        assert checksum == 0, (offset, 'checksum')
        index = frame[4]
        assert index < len(inputs), index
        actual = [int.from_bytes(frame[i:i + 4], 'big') for i in range(5, 45, 4)]
        want = expected(inputs[index])
        assert actual == want, dict(index=index, actual=actual, expected=want)
        seen.add(index)
        frames += 1
        offset += 48
    assert len(seen) == len(inputs), sorted(seen)
    return dict(status='PASS', frames=frames, inputCases=len(seen), valuesPerCase=10,
                bytes=len(data), uartSha256=hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.capture.read_bytes()), indent=2))
