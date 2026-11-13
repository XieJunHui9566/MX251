"""Board checks and timing for exact integer/float/integer scale chains."""
import argparse
from pathlib import Path
import struct
import machine_pipeline_fixture as board


INPUTS = [0,1,2,3,4,5,6,7,8,9,15,16,31,32,255,256,
          257,1023,1024,8191,8192,13107,13108,32767,32768,52428,
          52429,65000,65528,65529,65530,65531,65532,65533,65534,65535]
INPUTS += [(i*7919+12345)&65535 for i in range(64-len(INPUTS))]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def expected(kind, seed):
    x = INPUTS[seed]
    if not kind:
        return int(f32(f32(x)*f32(0.625)))
    values = [int(f32(f32(x)*f32(0.625))), int(f32(f32(x)*f32(0.375))),
              int(f32(f32(x)/f32(8))), int(f32(f32(x)*f32(3))),
              int(f32(f32(x&255)*f32(0.625))),
              int(f32(f32(x)*f32(0.625))), (x+1)&65535]
    h = 0x12345678
    for value in values:
        h = ((h<<5)^h^value)&0xffffffff
    return h


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    start = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', start)
    driver = driver[:start]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = scale_case(kind,input);
'''+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u32 scale_case(u8,u8);
u16 scale(u16);
int three_eighths(u16);
u16 eighth(u16);
u32 triple(u16);
u8 byte_scale(u8);
u16 volatile_scale(void);
extern volatile u16 observed;
''')
    (output/'scale.c').write_text('''#include "machine-board.h"
volatile u16 observed;
u16 scale(u16 x) { return x*0.625f; }
int three_eighths(u16 x) { return x*0.375f; }
u16 eighth(u16 x) { return (float)x/8; }
u32 triple(u16 x) { return x*3.0f; }
u8 byte_scale(u8 x) { return x*0.625f; }
u16 volatile_scale(void) { return observed++*0.625f; }
''')
    (output/'scale-case.c').write_text('''#include "machine-board.h"
static const u16 inputs[64]={'''+','.join(str(v)+'U' for v in INPUTS)+'''};
u32 scale_case(u8 kind,u8 seed) {
    u16 x=inputs[seed]; u32 h=0x12345678UL;
    if(!kind) return scale(x);
    h=(h<<5)^h^scale(x);
    h=(h<<5)^h^three_eighths(x);
    h=(h<<5)^h^eighth(x);
    h=(h<<5)^h^triple(x);
    h=(h<<5)^h^byte_scale((u8)x);
    observed=x;
    h=(h<<5)^h^volatile_scale();
    h=(h<<5)^h^observed;
    return h;
}
''')
    board.write(output/'mx251.json', dict(schema=2,target='AI8051U-34K64-32',
        model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,
        standard='sdcc11',runtime='native-math',defines=[]))
    (output/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(exact_float_scale NONE)
include("${MX251_PROJECT_MODULE}")
add_library(app INTERFACE)
target_sources(app INTERFACE machine-board.c scale-case.c scale.c machine-board.h)
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
