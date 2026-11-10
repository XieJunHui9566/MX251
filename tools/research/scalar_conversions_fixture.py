"""Exercise ordinary scalar facts through the production build and real board."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    def mix(h, v):
        return ((h << 5) ^ h ^ (v & 0xffffffff)) & 0xffffffff
    if not kind:
        values = [(2000 * 257) ^ seed, -285 ^ seed, 0xffffffff // 16000 + seed,
                  -2147483 ^ seed, (52 * 257) ^ seed, -13 ^ seed,
                  (0x2345 << 16) ^ 0xfffffff9 ^ 0xffffffff ^ seed]
    else:
        values = [4095, -285 ^ seed, ((65530 + seed) & 65535) ** 2, seed + 7,
                  (2000 if seed & 1 else 2001) ** 2 + seed, (0xa500 | seed) * 3]
    result = 0x13579bdf
    for value in values:
        result = mix(result, value)
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['scalar-conversions.h', 'scalar-conversions-callees.c', 'scalar-conversions-callers.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    source = (tests / 'machine-board.c').read_text().replace('machine-board.h', 'scalar-conversions.h')
    start, end = source.index('        x0 = '), source.index('        TH0 = ')
    source = source[:start] + source[end:]
    start, end = source.index('        if (kind == 0)'), source.index('        TR0 = 0; ticks')
    source = source[:start] + '        result = scalar_case(kind, input);\n' + source[end:]
    source = source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output / 'scalar-board.c').write_text(source, newline='\n')
    files.append('scalar-board.c')
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
