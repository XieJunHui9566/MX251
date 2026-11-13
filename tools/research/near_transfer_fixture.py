"""Mixed near/far calls, local objects, recursion, and ABI4 on AI8051U."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def leaf(x):
    x &= 65535
    return ((x ^ 0x1234) + (((x << 3) | (x >> 13)) & 65535)) & 65535


def recursive(x):
    return x + 17 if x < 3 else recursive(x - 3) + x


def expected(kind, seed):
    result = 0x2468ace0 if kind else 0x13579bdf
    for i in range(8):
        if kind:
            x = (seed * 1877 + i * 251) & 65535
            pair = lambda a, b: ((a + b) & 65535) ^ 0xa55a
            value = pair(x, x ^ 0x4321) ^ ((pair(x, x ^ 0x5678) + seed + i) & 65535)
            value = (value + (((x * 17) & 65535) ^ 0x3c69)) & 65535
        else:
            x = (seed * 1031 + i * 337) & 65535
            local = ((x ^ ((x + 19) & 65535) ^ (x ^ 0x55aa)) + leaf(x)) & 65535
            y = x ^ 0x5678
            stack = leaf(x + y) ^ ((x & 255) << 8) ^ (y & 255) ^ i ^ (x >> 8)
            value = ((local ^ stack) + recursive((seed + i) & 31)) & 65535
        result = ((result << 3) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ('near-transfers-cases.c', 'machine-board.h'):
        shutil.copy2(tests/name, output/name)
    main = (tests/'machine-board.c').read_text()
    main = main.replace('static u8 checksum;', 'u32 near_work(u16);\nu32 near_mixed(u16);\nstatic u8 checksum;')
    main = main.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    begin, end = main.index('        x0 ='), main.index('        TH0 =')
    main = main[:begin] + main[end:]
    begin, end = main.index('        if (kind == 0)'), main.index('        TR0 = 0; ticks')
    main = main[:begin] + '        result = kind ? near_mixed(input) : near_work(input);\n' + main[end:]
    (output/'board.c').write_text(main, newline='\n')
    units = [dict(name='u0', file='near-transfers-cases.c', backend='ordinary-wide', stackAuto=True, defines=[]),
             dict(name='u1', file='board.c', backend='legacy', stackAuto=True, defines=[])]
    files = ['near-transfers-cases.c', 'board.c', 'machine-board.h']
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
        runtime='standard', defines=[], files=files, units=units, archives=[], link=['u0', 'u1', '@startup']))
    board.write(output/'source.json', dict(files={f: board.sha(output/f) for f in files}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, expected))
