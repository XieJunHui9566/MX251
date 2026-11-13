"""Real Timer3/4 interrupts through fixed vector aliases after collection."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


def expected(kind, seed):
    return (0x12345678 + (seed + 1) * (17 if kind else 7)) & 0xffffffff


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    start = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', start)
    driver = driver[:start]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = vector_case(kind,input);
'''+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u32 vector_case(u8,u8);
''')
    (output/'vectors.c').write_text('''#include "machine-board.h"
__sfr __at(0xaf) IE2;
__sfr __at(0xdd) T4T3M;
__sfr __at(0xd4) T3H;
__sfr __at(0xd5) T3L;
__sfr __at(0xd2) T4H;
__sfr __at(0xd3) T4L;
__sfr __at(0xef) AUXINTIF;
__sbit __at(0xaf) EA;
static volatile u32 count;
static volatile u8 done;
static void add_count(u8 amount) { count += amount; done=1; }
void handler_a(void) __interrupt(13) { T4T3M=0; add_count(7); }
void handler_b(void) __interrupt(14) { T4T3M=0; add_count(17); }
float unused_division(float x, float y) { return x / y; }
u32 vector_case(u8 kind,u8 seed) {
    u8 i; u16 wait;
    count=0x12345678UL;
    for(i=0;i<=seed;++i) {
        EA=0; IE2=0; T4T3M=0; AUXINTIF &= 0xf9; done=0;
        T3H=0xff; T3L=0; T4H=0xff; T4L=0;
        IE2=kind ? 0x40 : 0x20;
        EA=1; T4T3M=kind ? 0x80 : 0x08;
        wait=65535;
        while(!done && --wait) {}
        EA=0; IE2=0; T4T3M=0;
        if(!done) return 0xdead0000UL | kind;
    }
    return count;
}
''')
    # AI8051U.H in official p059: Timer3/4 are slots 19/20 (0x9b/0xa3).
    # Place raw aliases there, beyond the compiler table for slots 13/14.
    # The hardware supplies the interrupt frame and RETI restores it normally.
    (output/'vectors.asm').write_text('''.module fixed_vectors
.area VECT (ABS,CODE)
.org 0xff009b
ljmp 0xff006b
.org 0xff00a3
ljmp 0xff0073
.area HOME (CODE)
.ds 481
''')
    board.write(output/'mx251.json', dict(schema=2,target='AI8051U-34K64-32',
        model='small',optimization='speed',backend='ordinary-wide',
        explicitAbi=False,standard='sdcc11',runtime='native-math',defines=[]))
    (output/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(vector_gc NONE)
include("${MX251_PROJECT_MODULE}")
add_library(app INTERFACE)
target_sources(app INTERFACE machine-board.c vectors.c vectors.asm machine-board.h)
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
