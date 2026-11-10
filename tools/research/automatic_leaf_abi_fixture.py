"""Cross-unit, observable byte arguments through the automatic leaf ABI build."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    result = 0x13579bdf
    for index in range(32):
        value = ((seed ^ (index * 7)) + index) & 255
        result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'leaf-write.c', 'leaf-caller.c', 'machine-control.c', 'machine-board.c']
    for name in ('machine-board.h', 'machine-control.c', 'machine-board.c'):
        shutil.copy2(tests / name, output / name)
    (output / 'leaf-write.c').write_bytes(b'''#include "machine-board.h"
volatile __xdata u8 observed[32];
void leaf_write(u8 index, u8 value)
{
    observed[index] = value;
}
''')
    source = '''#include "machine-board.h"
extern volatile __xdata u8 observed[32];
void leaf_write(u8, u8);
u32 leaf_case(u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL;
'''
    for i in range(32):
        source += f'    leaf_write({i}, (u8)((seed ^ {i * 7}) + {i}));\n'
    source += '''    for (i = 0; i < 32; ++i)
        result = (result << 5) ^ result ^ observed[i];
    return result;
}
'''
    (output / 'leaf-caller.c').write_bytes(source.encode())
    p = output / 'machine-board.c'
    text = p.read_text().replace('static u8 checksum;', 'u32 leaf_case(u8);\nstatic u8 checksum;')
    text = text.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = leaf_case(input);')
    p.write_bytes(text.encode())
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
