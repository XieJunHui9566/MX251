"""Check captured ARG1 frames against arithmetic independent of target code."""
import argparse
import hashlib
import json
from pathlib import Path


def expected(x):
    y = (x >> 7) ^ 0x96a55a69
    mask = 0xffffffff
    results = []
    for kind in ('byte', 'word', 'dword', 'native', 'pointer'):
        bits = 8 if kind == 'byte' else 16 if kind == 'word' else 32
        a, b = x & ((1 << bits) - 1), y & ((1 << bits) - 1)
        trace = 0
        for i in range(6):
            first, second = (a, b) if i % 2 == 0 else (b, a)
            value = ((first << 8) ^ second) & mask
            if kind == 'native':
                value ^= i + 1
            elif kind == 'pointer':
                value = ((x & 255) << 8) ^ (y & 255) ^ (y & 65535)
            trace = (((trace << 3) | (trace >> 29)) + value) & mask
        suffix = (y & 65535) if kind == 'pointer' else (a + b) & (65535 if bits <= 16 else mask)
        results.extend([value ^ 0xa5963cc3 ^ suffix, trace])
    return results


def check(data):
    inputs = [0, 1, 2, 3, 7, 0x80, 0x100, 0xff, 0xffff, 0x10000,
              0x7fffffff, 0x80000000, 0xaaaa5555, 0x5555aaaa,
              0xdeadbeef, 0xffffffff]
    offset = data.find(b'ARG1')
    assert offset >= 0, 'no ARG1 frame'
    seen = set()
    frames = 0
    while offset + 48 <= len(data):
        frame = data[offset:offset + 48]
        assert frame[:4] == b'ARG1' and frame[-2:] == b'\r\n', (offset, frame.hex())
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
