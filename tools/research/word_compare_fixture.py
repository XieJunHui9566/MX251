"""Signed/unsigned word-comparison boundaries on the AI8051U board."""
import argparse
import json
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


VALUES = sorted({v & 65535 for threshold in
    (0, 30, 1000, 1234, 0x1234, 0x3fff, 0x7fff, 0x8000, 0x8001, -1234, -1)
    for v in (threshold-1, threshold, threshold+1)})
VALUES += [(i*1031+337) & 65535 for i in range(64-len(VALUES))]


def mask(kind, value):
    if kind:
        x = value if value < 32768 else value-65536
        conditions = [x < -1234, x <= -1, x > 1234, x >= 32767,
                      x == -32768, x != -1234, x < 30, x >= 1000]
    else:
        x = value
        conditions = [x < 30, x <= 1000, x > 0x3fff, x >= 0x8000,
                      x == 0xffff, x != 0x8001, x < 0x1234, x >= 0x7fff]
    return sum(int(v) << bit for bit, v in enumerate(conditions))


def expected(kind, seed):
    result = 0x13579bdf
    for i in range(8):
        result = ((result << 3) ^ result ^ mask(kind, VALUES[(seed+i) & 63])) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ('word-compare-cases.c', 'machine-board.h'):
        shutil.copy2(tests/name, output/name)
    main = (tests/'machine-board.c').read_text()
    main = main.replace('static u8 checksum;',
        'u16 word_unsigned(u16);\nu16 word_signed(signed int);\n'
        'static const __code u16 values[64] = {' + ','.join(map(str, VALUES)) + '};\n'
        'static u8 checksum;')
    main = main.replace('u8 input, kind, overflow;', 'u8 input, kind, overflow, i;')
    main = main.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks, x;')
    begin, end = main.index('        x0 ='), main.index('        TH0 =')
    main = main[:begin] + main[end:]
    begin, end = main.index('        if (kind == 0)'), main.index('        TR0 = 0; ticks')
    main = main[:begin] + '''        result = 0x13579bdfUL;
        for (i = 0; i < 8; ++i) {
            x = values[(input + i) & 63];
            result = (result << 3) ^ result ^ (kind ? word_signed((signed int)x) : word_unsigned(x));
        }
''' + main[end:]
    (output/'board.c').write_text(main, newline='\n')
    units = [dict(name='u0', file='word-compare-cases.c', backend='ordinary-wide', stackAuto=True, defines=[]),
             dict(name='u1', file='board.c', backend='legacy', stackAuto=True, defines=[])]
    files = ['word-compare-cases.c', 'board.c', 'machine-board.h']
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
        runtime='standard', defines=[], files=files, units=units, archives=[], link=['u0', 'u1', '@startup']))
    board.write(output/'source.json', dict(values=VALUES, files={f:board.sha(output/f) for f in files}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data:board.check(data, expected))
