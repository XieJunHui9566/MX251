"""Check raw OBJ1 UART frames from object-call-effects-hardware.c."""
import argparse
import functools
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('capture', type=Path)
a = p.parse_args()
data = a.capture.read_bytes()
inputs = [0, 1, 2, 3, 7, 0x80, 0x100, 0xff, 0xffff, 0x10000,
          0x7fffffff, 0x80000000, 0xaaaa5555, 0x5555aaaa, 0xdeadbeef, 0xffffffff]
frames, pos, seen = 0, 0, set()
while True:
    pos = data.find(b'OBJ1', pos)
    if pos < 0:
        break
    packet = data[pos:pos + 20]
    pos += 20
    if len(packet) != 20:  # A capture can stop partway through the final frame.
        break
    assert packet[-2:] == b'\r\n', packet.hex()
    assert functools.reduce(lambda x, y: x ^ y, packet[:18], 0) == 0, packet.hex()
    i = packet[4]
    assert i < len(inputs), i
    x = inputs[i]
    y = (x >> 7) ^ 0x96a55a69
    first, second, third = x ^ 0x12345678, (y + 0x3456789a) & 0xffffffff, (x + y) & 0xffffffff
    value = ((first + second) & 0xffffffff) ^ third
    expected = [value ^ 0x87654321 ^ ((x ^ 0x5a) & 255) ^ (x & 1), value,
                ((x & 255) << 24) | (((x + 1) & 255) << 16)
                | (((x ^ 0x5a) & 255) << 8) | ((x ^ 0xa5) & 255)]
    got = [int.from_bytes(packet[5 + j * 4:9 + j * 4], 'big') for j in range(3)]
    assert got == expected, (i, got, expected)
    frames += 1
    seen.add(i)
assert seen == set(range(16)), (frames, sorted(seen))
print(json.dumps(dict(status='PASS', frames=frames, inputCases=len(seen), valuesPerCase=3,
                     uartSha256=hashlib.sha256(data).hexdigest()), indent=2))
