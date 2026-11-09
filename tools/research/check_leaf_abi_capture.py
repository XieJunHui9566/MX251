"""Check LAF1 UART frames against an independent byte-arithmetic oracle."""
import argparse
import hashlib
import json
from pathlib import Path


def check(data):
    at = data.find(b'LAF1')
    if at < 0:
        raise ValueError('no LAF1 frames')
    cases = set()
    frames = 0
    while at + 24 <= len(data):
        frame = data[at:at + 24]
        if frame[:4] != b'LAF1' or frame[-2:] != b'\r\n':
            raise ValueError('invalid frame boundary')
        checksum = 0
        for b in frame[:22]:
            checksum ^= b
        if checksum:
            raise ValueError('invalid checksum')
        x = frame[4]
        y = x ^ 0xa5
        expected = []
        for k in range(16):
            a, b = (x + k) & 255, (y - 7*k) & 255
            returned = ((a ^ ((b + 0x5b) & 255)) + (a >> 3)) & 255
            expected.append(returned ^ a ^ b)
        if list(frame[5:21]) != expected:
            raise ValueError(f'wrong call results for input {x}')
        cases.add(x)
        frames += 1
        at += 24
    if len(cases) != 256:
        raise ValueError(f'only {len(cases)} of 256 input cases received')
    return dict(status='PASS', frames=frames, inputCases=256, callsPerCase=16,
                uartSha256=hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.capture.read_bytes()), indent=2))
