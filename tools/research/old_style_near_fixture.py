"""Old-style empty declarations across units, ABI2/4, recursion and locals."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    state = seed * 1031 & 65535
    result = 0x13579bdf
    for i in range(8):
        state = (state * 17 & 65535) ^ 0x3c69
        if kind:
            value = ((state << 16) ^ 0x12345678) ^ ((state ^ 0x4321) + ((state ^ 0xa5) & 255))
        else:
            local = ((state ^ ((state + 19) & 65535) ^ (state ^ 0x55aa)) + state) & 65535
            n = (seed + i) & 7
            recursive = ((state ^ 0x1234) + n * (n + 1) // 2) & 65535
            value = local ^ recursive
        result = ((result << 3) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ('old-style-near-cases.c', 'machine-board.h'):
        shutil.copy2(tests/name, output/name)
    work = '''#include "machine-board.h"
extern volatile u16 old_state;
extern volatile u8 old_depth;
extern u16 (* volatile old_callback)(void);
void old_step();
u16 old_read();
u16 old_recursive();
u32 old_native() __sdcccall(4);
u8 old_byte();

u32 old_work(u16 seed, u8 kind)
{
    u8 i;
    u16 a, b;
    u32 value, result = 0x13579bdfUL;
    old_state = seed * 1031u;
    for (i = 0; i < 8; ++i) {
        old_step();
        if (kind) {
            value = old_native();
            a = old_callback(); b = old_byte();
            value ^= (u32)a + b;
        } else {
            a = old_read();
            old_depth = (seed + i) & 7;
            value = a ^ old_recursive();
        }
        result = (result << 3) ^ result ^ value;
    }
    return result;
}
'''
    (output/'work.c').write_text(work, newline='\n')
    main = (tests/'machine-board.c').read_text()
    main = main.replace('static u8 checksum;', 'u32 old_work(u16, u8);\nstatic u8 checksum;')
    main = main.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    begin, end = main.index('        x0 ='), main.index('        TH0 =')
    main = main[:begin] + main[end:]
    begin, end = main.index('        if (kind == 0)'), main.index('        TR0 = 0; ticks')
    main = main[:begin] + '        result = old_work(input, kind);\n' + main[end:]
    (output/'board.c').write_text(main, newline='\n')
    files = ['old-style-near-cases.c', 'work.c', 'board.c', 'machine-board.h']
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f == 'board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[:-1])]
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
        runtime='standard', defines=[], files=files, units=units, archives=[], link=['u0','u1','u2','@startup']))
    board.write(output/'source.json', dict(files={f: board.sha(output/f) for f in files}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, expected))
