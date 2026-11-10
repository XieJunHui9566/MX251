"""Exercise direct mask predicates against an independent integer reference."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    limit = 0xffffffff
    result = 0x13579bdf
    for i in range(16):
        x = (seed * 4 + i) & 255
        y = x ^ 0xc3
        wide = (x << 24) | ((x ^ 0x91) << 16) | ((x ^ 0x37) << 8) | (x ^ 0xa6)
        bits = wide
        for mask, operation, value in (
            (1, '^', 0x81731943), (2, '+', 0x12345679),
            (4, '^', 0x8193abcd), (8, '+', 0x2468abcd),
            (16, '^', 0x1098fedc), (32, '+', 0x76543211),
            (64, '^', 0x93abc721), (128, '+', 0x30417261),
            (0x8000, '^', 0x10293847), (0x10000, '+', 0xabcdef01),
            (0x80000000, '^', 0x91a2b3c4), (0x00f00000, '+', 0x27182819)):
            if wide & mask:
                bits = (bits + value if operation == '+' else bits ^ value) & limit
        if not wide & 0xff000000:
            bits ^= 0xfedcba98
        bits ^= wide
        loads = y
        if x & 0x80:
            loads += 0x12345
        if not x & 0x24:
            loads ^= 0xabcde
        if x & 0x40:
            loads += 0x98765
        if x & 0x10:
            loads ^= 0x773355
        if x & 0x80:
            loads += 0x112233
        if x & 1:
            loads ^= 0x573199
        else:
            loads += 0x319757
        loads ^= 0x772191  # The low byte of the volatile long is 0x21.
        if x & 0x80:
            loads += 0x111777
        y &= 0xa5
        if y:
            loads ^= 0x9715
        loads += y + x
        result = ((result << 3) ^ result ^ bits ^ loads) & limit
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'machine-control.c', 'predicate-branches.c', 'machine-board.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('static u8 checksum;', 'u32 pb_case(u8);\nstatic u8 checksum;')
    text = text.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = pb_case(input);')
    p.write_bytes(text.encode())
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f == 'machine-board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[1:])]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
                  backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
                  defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output / 'mx251.json', config)
    board.write(output / 'source.json', dict(files={f: board.sha(output / f) for f in files}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create':
        create(a.output)
    else:
        board.capture(a.hex, a.output, lambda data: board.check(data, expected))
