"""Execute byte streams and 8/16/32-bit shift/carry pairs on both MCS251 backends."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board

EDGES = [0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff,
         0x7fffffff, 0x80000000, 0xffffffff, 0x55555555,
         0xaaaaaaaa, 0x80000001, 0xfffffffe, 0x12345678]


def expected(kind, seed):
    result = 0x13579bdf
    def mix(value):
        nonlocal result
        result = ((result << 3) ^ value) & 0xffffffff
    def shifted(value, width, left):
        value &= (1 << width)-1
        carry = value >> (width-1) if left else value & 1
        output = (value << 1) & ((1 << width)-1) if left else value >> 1
        return output ^ (0x9e3779b9 if carry else 0)
    for high in range(4):
        value = seed+high*64
        mix(value)  # Eight outgoing bits reconstruct the original byte.
        mix(shifted(value, 8, True))
        mix(shifted(value, 8, False))
    value = EDGES[seed & 15] ^ ((seed >> 4)*0x01010101)
    for width in (16, 32):
        for left in (True, False):
            mix(shifted(value, width, left))
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    shutil.copy2(tests/'machine-board.h', output/'machine-board.h')
    shutil.copy2(tests/'carry-observation.c', output/'carry-cases.h')
    text = (tests/'machine-board.c').read_text()
    text = text.replace('#include "machine-board.h"', '#include "machine-board.h"\n'
        'u16 POINT_COLOR; u32 trace;\nu32 legacy_work(u8);\nu32 wide_work(u8) __sdcccall(4);')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;',
                        'result = legacy_work(input);').replace('else result = control(input);',
                        'else result = wide_work(input);')
    (output/'machine-board.c').write_text(text)
    for prefix in ('legacy', 'wide'):
        (output/(prefix+'.c')).write_text('#define FN(name) '+prefix+'_##name\n'+
            ('#define ABI __sdcccall(4)\n' if prefix == 'wide' else '')+'#include "carry-cases.h"\n')
    files = ['machine-board.h', 'carry-cases.h', 'legacy.c', 'wide.c', 'machine-board.c']
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide' if f == 'wide.c' else 'legacy',
                  stackAuto=True, defines=[]) for i,f in enumerate(files[2:])]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
        backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
        defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output/'mx251.json', config)
    board.write(output/'source.json', dict(files={f:board.sha(output/f) for f in files}))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(carry_observation NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    command = sub.add_parser('create'); command.add_argument('--output', type=Path, required=True)
    command = sub.add_parser('capture')
    command.add_argument('--hex', type=Path, required=True); command.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.output)
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, expected))
