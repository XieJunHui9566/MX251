"""Loop cursor build inputs and independent board result checking."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    a = [7,11,19,251,128,0,99,37,62,41,83,93,15,214,38,76,
         87,211,129,51,28,10,199,137,162,141,183,193,115,14,138,176]
    b = [131,22,241,17,79,182,193,4,58,96,166,254,145,23,0,6,
         31,122,41,117,179,82,93,104,158,196,66,154,45,123,100,106]
    if kind:
        a = [(i & 255) ^ seed for i in range(260)]
        b = [(v + 3) & 255 for v in a]
    result = 0x13579bdf ^ seed
    for x, y in zip(a, b):
        for value in (x, y):
            result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result ^ len(a)


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['loop-cursors.h', 'loop-cursors.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    source = (tests / 'machine-board.c').read_text().replace('machine-board.h', 'loop-cursors.h')
    start, end = source.index('        x0 = '), source.index('        TH0 = ')
    source = source[:start] + source[end:]
    start, end = source.index('        if (kind == 0)'), source.index('        TR0 = 0; ticks')
    source = source[:start] + '        result = cursor_case(kind, input);\n' + source[end:]
    source = source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output / 'cursor-board.c').write_text(source, newline='\n')
    files.append('cursor-board.c')
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i, f in enumerate(files) if f.endswith('.c')]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
        backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
        defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output / 'mx251.json', config)
    board.write(output / 'source.json', dict(files={f:board.sha(output / f) for f in files}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('capture'); p.add_argument('--hex', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.output)
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, expected))
