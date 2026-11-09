"""Read-only verification of a captured binary UART stream, no hardware access."""
import argparse
import json
from pathlib import Path
import struct

p = argparse.ArgumentParser()
p.add_argument('capture', type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
inputs = [0, 1, 2, 3, 7, 0x80, 0x100, 0xff, 0xffff, 0x10000,
          0x7fffffff, 0x80000000, 0xaaaa5555, 0x5555aaaa, 0xdeadbeef, 0xffffffff]
raw = a.capture.read_bytes()
seen, counts, bad, errors = {}, {}, 0, []
start = 0
while (start := raw.find(b'BCR1', start)) >= 0:
    frame = raw[start:start + 40]
    start += 1
    if len(frame) < 40:
        break
    checksum = 0
    for b in frame[:38]:
        checksum ^= b
    if checksum or frame[-2:] != b'\r\n' or frame[4] >= 16:
        bad += 1
        continue
    i = frame[4]
    got = list(struct.unpack('>8I', frame[5:37]))
    x = inputs[i]; y = (x >> 7) ^ 0x96a55a69
    expected = [x & 1, (x + y + ((x ^ y ^ 0x1234) & 1)) & 0xffffffff,
                (x + 7 if x & 1 else x - 9) & 0xffffffff,
                (x & 255).bit_count(), (x + 1) & 0xffffffff,
                x & 1, x & 65535, 29]
    if got != expected:
        errors.append(dict(case=i, actual=got, expected=expected))
    seen[i] = got
    counts[i] = counts.get(i, 0) + 1
result = dict(passed=len(seen) == 16 and not errors and not bad,
              cases=len(seen), values=len(seen) * 8, frames=sum(counts.values()),
              invalidFrames=bad, repeats=counts, errors=errors,
              limits='Functional probe only; no SDK timing or Flash readback claim.')
a.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(0 if result['passed'] else 1)
