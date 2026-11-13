"""Board checks for EDATA locals, CODE initializers, math names and 32 KiB XRAM."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    start = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', start)
    driver = driver[:start]+'        result = compatibility_case(kind,input);\n'+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\nu32 compatibility_case(u8,u8);\n')
    (output/'cases.c').write_text(r'''
#include "machine-board.h"
#include <math.h>
volatile __xdata u8 bank[32768];
static u8 first = 1;
struct Font {u8 text[2]; u8 dots[3];};
const struct Font font[] = {"ab",1,2,3,"cd",4,5,6};
u8 __code descriptor[3];
u8 __code descriptor[3] = {7,8,9};
u8 __code zeros[3];

static u32 nested(u8 n) __reentrant
{
    volatile __edata u32 value = 0x12340000UL+n;
    volatile u32 *p = &value;
    u32 lower = n ? nested(n-1) : 0;
    return *p+lower;
}

u32 compatibility_case(u8 kind, u8 seed)
{
    u16 i, begin = (u16)seed*512;
    u32 failed = 0;
    if (!kind) {
        if (first) {
            for (i=0; i<32768; ++i) if (bank[i]) failed |= 1;
            first = 0;
        }
        if (!seed)
            for (i=0; i<32768; ++i) bank[i] = (u8)(i^(i>>8)^0xa5);
        for (i=begin; i<begin+512; ++i)
            if (bank[i] != (u8)(i^(i>>8)^0xa5)) failed |= 2;
    } else {
        u8 n=seed&7;
        volatile float x=(float)(seed+1);
        if (nested(n) != (u32)(n+1)*0x12340000UL+(u16)n*(n+1)/2) failed |= 4;
        if (descriptor[seed%3] != 7+seed%3 || zeros[seed%3]) failed |= 8;
        if (font[seed&1].text[0] != ((seed&1)?'c':'a')) failed |= 16;
        if (font[seed&1].dots[seed%3] != 1+(seed&1)*3+seed%3) failed |= 32;
        if (sqrt(x*x) != x || fabs(-x) != x) failed |= 64;
    }
    return failed;
}
''', encoding='ascii')
    board.write(output/'mx251.json', dict(schema=2,target='AI8051U-34K64-32',
        model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,
        standard='sdcc11',runtime='native-math',defines=[]))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(sdk_compatibility NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE machine-board.c cases.c machine-board.h)\nmx251_firmware(app)\n')
    board.write(output/'source.json', {p.name:board.sha(p) for p in output.iterdir() if p.is_file()})


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,lambda kind,seed:0))
