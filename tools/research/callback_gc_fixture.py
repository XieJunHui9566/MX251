"""Exercise cross-unit, table and runtime-selected callbacks after function GC."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


def expected(kind, seed):
    value = 0x12340000+seed
    which = ((seed >> 1) if kind else seed) & 1
    return ((value ^ 0x55aa33cc) if which else value+0x10203) & 0xffffffff


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    source = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    start, end = source.index('        x0 ='), source.index('        TR0 = 0; ticks')
    source = source[:start]+'        result = callback_case(kind,input);\n'+source[end:]
    source = source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(source)
    (output/'machine-board.h').write_text('''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef u32 (*callback_t)(u32);
u32 callback_case(u8,u8);
u32 add_value(u32);
u32 xor_value(u32);
callback_t choose(u8);
''')
    (output/'callbacks.c').write_text('''#include "machine-board.h"
u32 add_value(u32 v) {return v+0x10203UL;}
u32 xor_value(u32 v) {return v^0x55aa33ccUL;}
callback_t choose(u8 n) {return n & 2 ? xor_value : add_value;}
float unused_callback_math(float x,float y) {return x/y;}
''')
    (output/'dispatch.c').write_text('''#include "machine-board.h"
callback_t const __code table[] = {add_value,xor_value};
static callback_t volatile selected;
u32 callback_case(u8 kind,u8 seed) {
    selected=kind ? choose(seed) : table[seed&1];
    return selected(0x12340000UL+seed);
}
''')
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',
        model='small',optimization='speed',backend='ordinary-wide',
        explicitAbi=False,standard='sdcc11',runtime='standard',defines=[]))
    (output/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(callback_gc NONE)
include("${MX251_PROJECT_MODULE}")
add_library(app INTERFACE)
target_sources(app INTERFACE machine-board.c machine-board.h callbacks.c dispatch.c)
mx251_firmware(app)
''')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
