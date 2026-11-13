"""Normal build pipeline: mixed float varargs and default EDATA on AI8051U."""
import argparse
import math
from pathlib import Path
import struct
import machine_pipeline_fixture as board
from math_transport_fixture import VECTORS


def expected(kind, seed):
    raw = VECTORS[seed]
    value = struct.unpack('>f', raw.to_bytes(4, 'big'))[0]
    pattern = ('.2f', '.9F', '020.0f', 'f')[seed & 3]
    if math.isfinite(value):
        number = format(value, pattern)
    else:
        number = ('-' if raw >> 31 else '') + ('nan' if raw & 0x7fffff else 'inf')
        if seed & 3 == 1:
            number = number.upper()
        if seed & 3 == 2:
            number = number.rjust(20)
    text = f'{seed-32}|{12345678+seed}|{number}|{chr(65+seed%26)}'.encode()
    result = len(text)
    for c in text+b'\0':
        result = ((result << 5) ^ result ^ c) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    driver = (tests/'machine-board.c').read_text()
    start = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', start)
    driver = driver[:start]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = format_case(kind,input);
'''+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u32 format_case(u8,u8);
u8 storage_check(void);
extern volatile u8 scratch[300];
''')
    (output/'storage.c').write_text('''#include "machine-board.h"
volatile u8 scratch[300];
volatile u8 initial[5]={0x23,0x45};
volatile u16 word=0x6789;
volatile u8 *saved=&scratch[299];
volatile __data u8 direct=0x56;
volatile __xdata u8 external=0x78;
volatile __edata u8 explicit_edata=0x9a;
u8 __sdcc_external_startup(void) {
    volatile u8 *p=(volatile u8 *)0x100;
    while(p<(volatile u8 *)0x400) *p++=0xdd;
    return 0;
}
u8 storage_check(void) {
    static u8 local_zero;
    static u16 local_value=0xabcd;
    u16 i;
    if(initial[0]!=0x23 || initial[1]!=0x45 || initial[2] || initial[4] ||
       word!=0x6789 || saved!=&scratch[299] || direct!=0x56 || external!=0x78 ||
       explicit_edata!=0x9a || local_zero || local_value!=0xabcd) return 0;
    for(i=0;i<300;++i) if(scratch[i]) return 0;
    *saved=0xa5; i=scratch[299]==0xa5; *saved=0;
    return (u8)i;
}
''')
    source = '''#include "machine-board.h"
#include <stdio.h>
static char output[300];
static u16 used;
static const u32 inputs[64]={
'''+','.join('0x%08xUL' % v for v in VECTORS)+'''};
int putchar(int c) { output[used++]=(char)c; return c; }
u32 format_case(u8 kind,u8 seed) {
    union { float f; u32 u; } x;
    u16 i; int n; u32 h;
    if(!storage_check()) return 0xdead0000UL;
    x.u=inputs[seed]; used=0;
'''
    for kind, api in ((0, 'sprintf'), (1, 'printf')):
        source += ('    if' if not kind else '    else if')+f'(kind=={kind}) {{\n'
        for i, pattern in enumerate(('%0.2f', '%.9F', '%020.0f', '%f')):
            source += ('        if' if not i else '        else if')+f'((seed&3)=={i}) n={api}('
            source += ('output,' if not kind else '')+f'"%d|%lu|{pattern}|%c",(int)seed-32,12345678UL+seed,x.f,(int)(65+seed%26));\n'
        source += '    }\n'
    source += '''    if(kind) output[used]=0;
    h=n; for(i=0;i<=(u16)n;++i) h=(h<<5)^h^(u8)output[i];
    return h;
}
'''
    (output/'format-case.c').write_text(source)
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',
        defaultDataSpace='edata',optimization='speed',backend='ordinary-wide',explicitAbi=False,
        standard='sdcc11',runtime='native-math',defines=[]))
    (output/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(format_edata NONE)
include("${MX251_PROJECT_MODULE}")
add_library(app INTERFACE)
target_sources(app INTERFACE machine-board.c format-case.c storage.c machine-board.h)
mx251_firmware(app)
''')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    s=p.add_subparsers(dest='command',required=True)
    c=s.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=s.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
