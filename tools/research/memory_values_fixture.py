"""Independent memory forwarding, alias, partial store and call execution test."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board
import shared_cost_batch_fixture as batch


def expected(kind, seed):
    if kind:
        values = [0x5aa5, ((seed*257+0x1234)&0xff00)|0x5a,
                  seed+0x4312, seed^0xa55a, (seed+1)^seed,
                  seed+(19 if seed&1 else 7), (seed&0xff00)|0xab]
    else:
        initial = (0x2000+seed*257)&65535
        values = [0x2345eecc, ((initial+seed)&65535)^initial,
                  2*seed+1, (seed-32)&0xffffffff]
    result = 0x13579bdf
    for value in values:
        result = ((result*33)^value)&0xffffffff
    return result


def create(output, model, abi):
    batch.create(output, model)
    for name in ('memory-values.c', 'memory-values.h'):
        shutil.copy2(board.REPO/'core/sources/compiler/src/mcs251/tests'/name, output/name)
    (output/'mutate.c').write_text('#include "memory-values.h"\n'
        'void mv_change(mv_u16 value) MV_ABI {mv_a=value;}\n', newline='\n')
    (output/'cases.c').write_text('''#include "machine-board.h"
#include "memory-values.h"
u32 batch_case(u8 kind,u8 seed) {
    u32 result=0x13579bdfUL;
    if(!kind) {
        result=(result*33UL)^mv_constant();
        result=(result*33UL)^mv_forward(seed);
        result=(result*33UL)^mv_redefine(seed);
        result=(result*33UL)^mv_signed((signed char)(seed-32));
    } else {
        result=(result*33UL)^mv_alias(seed,(__xdata mv_u16 *)&mv_a);
        result=(result*33UL)^mv_partial(seed);
        result=(result*33UL)^mv_call(seed);
        result=(result*33UL)^mv_callback(seed,mv_change);
        result=(result*33UL)^mv_volatile(seed);
        result=(result*33UL)^mv_branch(seed);
        result=(result*33UL)^mv_overlap(seed);
    }
    return result;
}
''', newline='\n')
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32',
        model=model, optimization='speed', backend='ordinary-wide', explicitAbi=True,
        standard='sdcc11', runtime='standard', defines=['TEST_ABI='+str(abi)]))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\n'
        'project(memory_values NONE)\ninclude("${MX251_PROJECT_MODULE}")\n'
        'add_library(app INTERFACE)\ntarget_sources(app INTERFACE '
        'machine-board.c cases.c mutate.c memory-values.c memory-values.h machine-board.h)\n'
        'mx251_firmware(app)\n', newline='\n')
    board.write(output/'source.json', {p.name: board.sha(p) for p in output.iterdir()
                                     if p.is_file() and p.name!='source.json'})


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command', required=True)
    p=sub.add_parser('create');p.add_argument('--output', type=Path, required=True)
    p.add_argument('--model', choices=['small','large'], default='small')
    p.add_argument('--abi', type=int, choices=[2,4], default=2)
    p=sub.add_parser('capture');p.add_argument('--hex', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    if args.command=='create':create(args.output,args.model,args.abi)
    else:board.capture(args.hex,args.output,lambda data:board.check(data,expected))
