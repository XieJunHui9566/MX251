"""Generate a standalone bitmap-sharing board fixture and check its UART trace.

No SDK source is modified or required. Glyphs are synthetic. Each case records
every renderer callback and its arguments, including a nested renderer call.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics


def glyph(k, i):
    return ((k * 37 + i * 13) ^ (i >> 1) ^ 0xa5) & 255


def create(root):
    root.mkdir(parents=True, exist_ok=False)
    tests = Path(__file__).resolve().parents[2] / 'core/sources/compiler/src/mcs251/tests'
    for name in ('function-sharing-render.c', 'function-sharing-board.c'):
        (root / name).write_bytes((tests / name).read_bytes())
    lines = ['typedef unsigned char u8;', 'typedef unsigned int u16;']
    for letter, side, count in [('a', 16, 58), ('b', 24, 8), ('c', 32, 4)]:
        size = side * side // 8
        lines.append(f'static const struct glyph_{letter} {{ u8 dat[{size}], txt[2]; }} __code glyph_{letter}[{count}] = {{')
        for k in range(count):
            data = ','.join(str(glyph(k, i)) for i in range(size))
            lines.append('{{' + data + '},{' + f'{0x80+k},{0x90+k}' + '}},')
        lines.append('};')
    (root / 'function-sharing-data.h').write_text('\n'.join(lines) + '\n', encoding='ascii')
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
                  backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
                  defines=[], files=['function-sharing-data.h', 'function-sharing-render.c', 'function-sharing-board.c'],
                  units=[dict(name='u0', file='function-sharing-render.c', backend='ordinary-wide', stackAuto=True, defines=[]),
                         dict(name='u1', file='function-sharing-board.c', backend='legacy', stackAuto=True, defines=[])],
                  archives=[], link=['u0', 'u1', '@startup'])
    (root / 'mx251.json').write_text(json.dumps(config, indent=2) + '\n')


def expected(case):
    trace, count, color, nested = 0x12345678, 0, 0x7654, case >= 48

    def record(*values):
        nonlocal trace, count
        for value in values:
            trace = ((trace << 5) ^ trace ^ (value & 65535)) & 0xffffffff
            count += 1

    def window(x, y, right, bottom):
        nonlocal nested
        record(1, x, y, right, bottom)
        if nested:
            nested = False
            render(24, 3, 123, 234, 0x5aa5, 0xaa55, 1)

    def render(side, k, x, y, fc, bc, mode):
        nonlocal color
        x0 = x
        if k != 255:
            window(x, y, x + side - 1, y + side - 1)
            for i in range(side * side // 8):
                for j in range(8):
                    bit = glyph(k, i) & (0x80 >> j)
                    if not mode:
                        record(2, fc if bit else bc)
                    else:
                        color = fc
                        if bit:
                            record(3, x, y, color)
                        x = (x + 1) & 65535
                        if (x - x0) & 65535 == side:
                            x = x0
                            y = (y + 1) & 65535
                            break
        window(0, 0, 319, 239)

    which = case % 3
    size, entries = [(16, 58), (24, 8), (32, 4)][which]
    slot = [255, 0, entries // 2, entries - 1][(case // 6) & 3]
    render(size, slot, (0xfff0 + case) & 65535, (0xffe0 + case) & 65535,
           0x5123 ^ case, 0xcba9 + case, (case // 3) & 1)
    return trace, count, color


def check(path):
    data = path.read_bytes()
    offset = data.find(b'FSH1')
    assert offset >= 0, 'no FSH1 frame'
    times = {case: [] for case in range(96)}
    frames = 0
    while offset + 20 <= len(data):
        frame = data[offset:offset + 20]
        assert frame[:4] == b'FSH1' and frame[-2:] == b'\r\n', (offset, frame.hex())
        checksum = 0
        for byte in frame[:18]:
            checksum ^= byte
        assert checksum == 0, (offset, 'checksum')
        case = frame[4]
        actual = (int.from_bytes(frame[5:9], 'big'), int.from_bytes(frame[9:11], 'big'),
                  int.from_bytes(frame[11:13], 'big'))
        assert actual == expected(case), (case, actual, expected(case))
        assert frame[15] == 0 and frame[16] == 1, (case, 'timer overflow or entry identity', frame.hex())
        times[case].append(int.from_bytes(frame[13:15], 'big'))
        frames += 1
        offset += 20
    assert all(times.values()), {k: len(v) for k, v in times.items()}
    return dict(status='PASS', cases=96, frames=frames, uartSha256=hashlib.sha256(data).hexdigest(),
                ticks={k: statistics.median(v) for k, v in times.items()})


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--create', type=Path)
    p.add_argument('--capture', type=Path)
    a = p.parse_args()
    if a.create:
        create(a.create)
    if a.capture:
        print(json.dumps(check(a.capture), indent=2))
