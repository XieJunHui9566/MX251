"""Independent mixed-width pressure, physical copies and cross-call values."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board
import shared_cost_batch_fixture as batch
import value_copy_fixture


def expected(kind, seed):
    if kind:
        return value_copy_fixture.expected(0, seed)
    mask=0xffffffff
    p=[((0x81234567+(i+1)*0x01020304)^(seed*0x01010101))&mask for i in range(9)]
    x=(0xffffff00+seed*4099)&mask
    a=[((x+k)^(x>>(k&3)))&255 for k in range(7)]
    b,c,d,e=p[0]&255,p[1]&255,p[2]&65535,p[3]&65535
    f,g,h,i,j=p[4:]
    q=sum(a)+(b+(d>>3)+f+(h^j) if x&1 else c+(e>>5)+g+(i^j))
    mixed=(q+(b^c)+(d^e)+f+g+h+i+j+a[x&3])&mask
    consume=(sum((x+k)&255 for k in range(40))+x+(((x+1)&mask)^x)+((x+2)&mask))&mask
    b,c,d,e,f,g,h,i,j=p
    spilled=(consume+b+(c>>1)+(d>>2)+(e>>3)+f+(g>>1)+(h>>2)+(i>>3)+j+(b^c^d^e^f^g^h^i^j))&mask
    return ((mixed*33)^spilled)&mask


def create(output, model):
    batch.create(output,model)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    names=['wide-spill-allocation.c','value-copy.h','value-copy-access.c','value-copy-sinks.c','value-copy-cases.c']
    for name in names:shutil.copy2(tests/name,output/name)
    (output/'cases.c').write_text('''#include "value-copy.h"
u32 p_mixed(u32 *,u32) __reentrant __sdcccall(4);
u32 p_spill(u32 *,u32) __reentrant __sdcccall(4);
static u32 pressure[9];
u32 batch_case(u8 kind,u8 seed) {
    u8 k;u32 a,b,x;
    if(kind) {copy_init();return copy_case(seed);}
    for(k=0;k<9;++k)pressure[k]=(0x81234567UL+(k+1)*0x01020304UL)^((u32)seed*0x01010101UL);
    x=0xffffff00UL+(u32)seed*4099UL;
    a=p_mixed(pressure,x);b=p_spill(pressure,x);
    return (a*33UL)^b;
}
''',newline='\n')
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\n'
        'project(register_affinity NONE)\ninclude("${MX251_PROJECT_MODULE}")\n'
        'add_library(app INTERFACE)\ntarget_sources(app INTERFACE machine-board.c machine-board.h cases.c '
        +' '.join(names)+')\nmx251_firmware(app)\n',newline='\n')
    board.write(output/'source.json',{p.name:board.sha(p) for p in output.iterdir()
                                    if p.is_file() and p.name!='source.json'})


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('create');p.add_argument('--output',type=Path,required=True)
    p.add_argument('--model',choices=['small','large'],default='small')
    p=sub.add_parser('capture');p.add_argument('--hex',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.command=='create':create(args.output,args.model)
    else:board.capture(args.hex,args.output,lambda data:board.check(data,expected))
