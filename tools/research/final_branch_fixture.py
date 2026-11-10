"""Sparse 32-bit dispatch with observable stores and an independent oracle."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board

KEYS = [0x123, 0x456, 0x789, 0xabc, 0x1234, 0x2345, 0x3456,
        0x4567, 0x5678, 0x6789, 0x789a, 0x12345, 0x23456, 0x34567]


def inputs():
    return KEYS + [key ^ 1 for key in KEYS] + [0, 0x7fffffff, 0x80000001, 0xfffffffe]


def expected(kind, seed):
    if kind: return board.expected(kind, seed)
    result = 0x13579bdf
    for i in range(16):
        key = inputs()[(seed+i) & 31]
        if key in KEYS:
            n = KEYS.index(key)
            a, b = (n*17+3) & 255, (n*11+7) & 255
        else:
            value = key // 13
            a, b = value & 255, (value >> 8) & 255
        result = ((result << 3) ^ result ^ (a << 8) ^ b) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'branch-dispatch.c', 'branch-caller.c', 'machine-control.c', 'machine-board.c']
    for name in ('machine-board.h', 'machine-control.c', 'machine-board.c'):
        shutil.copy2(tests/name, output/name)
    source = '#include "machine-board.h"\nvolatile __data u8 observed_a, observed_b;\nvoid branch_dispatch(u32 key)\n{\n    switch (key) {\n'
    for n, key in enumerate(KEYS):
        source += f'    case 0x{key:08x}UL: observed_a = {(n*17+3)&255}; observed_b = {(n*11+7)&255}; return;\n'
    source += '    default: key /= 13; observed_a = (u8)key; observed_b = (u8)(key >> 8);\n    }\n}\n'
    (output/'branch-dispatch.c').write_bytes(source.encode())
    source = '#include "machine-board.h"\nextern volatile __data u8 observed_a, observed_b;\nvoid branch_dispatch(u32);\n'
    source += 'static const __code u32 keys[32] = {' + ','.join(f'0x{k:08x}UL' for k in inputs()) + '};\n'
    source += '''u32 branch_case(u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL;
    for (i = 0; i < 16; ++i) {
        branch_dispatch(keys[(seed+i) & 31]);
        result = (result << 3) ^ result ^ ((u16)observed_a << 8) ^ observed_b;
    }
    return result;
}
'''
    (output/'branch-caller.c').write_bytes(source.encode())
    p = output/'machine-board.c'
    source = p.read_text().replace('static u8 checksum;', 'u32 branch_case(u8);\nstatic u8 checksum;')
    source = source.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    source = source.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = branch_case(input);')
    p.write_bytes(source.encode())
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i, f in enumerate(files[1:])]
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
        runtime='standard', defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json', dict(files={f: board.sha(output/f) for f in files}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('capture'); p.add_argument('--hex', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create': create(args.output)
    else: board.capture(args.hex, args.output, lambda data: board.check(data, expected))
