"""Board comparison of MDU arithmetic plus empty/mixed 32-bit formatting."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


def expected(kind, seed):
    a=((seed+1)*0x1020304)&0xffffffff
    value=a//(seed+3) if kind else (a*(seed+3))&0xffffffff
    signed=(0x80000000+value)&0xffffffff
    if signed&0x80000000: signed-=0x100000000
    text=f'{value} {signed} {value:08x}'
    result=len(text)
    for c in text: result=(result*33+ord(c))&0xffffffff
    return result


def check(data):
    result=board.check(data,expected)
    # This fixture uses the frame transport, not the benchmark timer window.
    result.pop('ticks')
    result.pop('aggregateTicks')
    result['timingMeasured']=False
    return result


def create(output):
    output.mkdir(parents=True,exist_ok=False)
    driver=(board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    begin=driver.index('        x0 =')
    end=driver.index('        TR0 = 0; ticks',begin)
    driver=driver[:begin]+'        result = format_case(kind,input);\n'+driver[end:]
    driver=driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\nu32 format_case(u8,u8);\n')
    (output/'cases.c').write_text(r'''
#include "machine-board.h"
#include <stdio.h>
static char text[48], empty[1];
u32 format_case(u8 kind,u8 seed)
{
    volatile u32 a=(seed+1UL)*0x1020304UL;
    volatile u32 b=seed+3;
    u32 value=kind ? a/b : a*b;
    long signed_value=(long)(0x80000000UL+value);
    u32 result;
    u8 i;
    empty[0]='x';
    if(sprintf(empty,"")!=0 || empty[0]) return 0xbadUL;
    result=sprintf(text,"%lu %ld %08lx",value,signed_value,value);
    for(i=0;text[i];++i) result=result*33UL+(u8)text[i];
    return result;
}
''',encoding='ascii')
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',
        model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,
        standard='sdcc11',runtime='native-int-mdu32',defines=[]))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(integer_format_mdu NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE machine-board.c cases.c machine-board.h)\nmx251_firmware(app)\n')
    board.write(output/'source.json',{p.name:board.sha(p) for p in output.iterdir() if p.is_file()})


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,check)
