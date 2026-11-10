"""Non-leaf u8/u16 register arguments live across calls, with a raw ISR vector."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    result, bus = 0x13579bdf, 0x2468
    for index in range(32):
        value = ((seed * 257) ^ (index * 2053)) & 65535
        for octet in (value >> 8, value & 255):
            bus = ((bus << 3) ^ octet ^ 0x5a5a) & 65535
        result = ((result << 5) ^ result ^ (value ^ bus)) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'call-write.c', 'call-emit.c', 'call-caller.c',
             'machine-control.c', 'machine-board.c', 'vectors.asm']
    for name in ('machine-board.h', 'machine-control.c', 'machine-board.c'):
        shutil.copy2(tests / name, output / name)
    (output / 'call-write.c').write_text('''#include "machine-board.h"
extern volatile __xdata u16 bus;
volatile __xdata u16 observed[32];
void emit(u8);
void call_write(u8 index, u16 value)
{
    emit((u8)(value >> 8));
    emit((u8)value);
    observed[index] = value ^ bus;
}
''', newline='\n')
    (output / 'call-emit.c').write_text('''#include "machine-board.h"
volatile __xdata u16 bus;
void emit(u8 value)
{
    bus = (bus << 3) ^ value ^ 0x5a5a;
}
void probe_irq(void) __interrupt(0)
{
}
''', newline='\n')
    source = '''#include "machine-board.h"
extern volatile __xdata u16 bus, observed[32];
void call_write(u8, u16);
u32 call_case(u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL;
    bus = 0x2468;
'''
    for i in range(32):
        source += f'    call_write({i}, ((u16)seed * 257u) ^ {i * 2053}u);\n'
    source += '''    for (i = 0; i < 32; ++i)
        result = (result << 5) ^ result ^ observed[i];
    return result;
}
'''
    (output / 'call-caller.c').write_text(source, newline='\n')
    (output / 'vectors.asm').write_text('''.module call_vectors
.area CALLVECT (ABS,CODE)
.org 0xff01d3
ljmp 0xff0003
.area HOME (CODE)
.ds 481
''', newline='\n')
    p = output / 'machine-board.c'
    text = p.read_text().replace('static u8 checksum;', 'u32 call_case(u8);\nstatic u8 checksum;')
    text = text.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;',
                        'result = call_case(input);')
    p.write_text(text, newline='\n')
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i, f in enumerate(files[1:])]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
                  backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
                  defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output/'mx251.json', config)
    board.write(output/'source.json', dict(files={f: board.sha(output/f) for f in files}))


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
