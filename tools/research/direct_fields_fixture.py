"""Check native DATA fields, unaligned offsets, aliases and byte neighbors."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    result, count, total = 0x13579bdf, (0xffe0 + seed) & 65535, 0x81527319
    for i in range(8):
        value = (seed << 8) | (seed + i)
        count = (count + value) & 65535
        total ^= (count << 16) | value
        last = count ^ value
        step = (total + last) & 0xffffffff
        result = (((result << 3) ^ result) & 0xffffffff) ^ step
        count ^= seed + i
        # The aliased byte at offset 4 is the second byte of the dword.
        total = (total & 0xff00ffff) | (((total >> 16) + seed + i) & 255) << 16
        total ^= 0x91
        result += count
        result ^= total
        volatile = ((value << 16) | (value ^ 0x5197)) ^ value ^ ((value + 17) & 65535) ^ 0x81 ^ 0x63 ^ 0xa5
        result = (result + volatile) & 0xffffffff
    return result ^ 0xa719e300 ^ 0xa519 ^ 0x18e7 ^ 0x7123


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'direct-fields.h', 'direct-fields-access.c',
             'direct-fields-objects.c', 'machine-control.c', 'machine-board.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('static u8 checksum;', 'u32 df_case(u8);\nstatic u8 checksum;')
    text = text.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = df_case(input);')
    p.write_bytes(text.encode())
    units = [dict(name='u' + str(i), file=f,
                  backend='legacy' if f in ('machine-board.c', 'direct-fields-objects.c') else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[2:])]
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
