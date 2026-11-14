"""SFR bit readback with live arithmetic, calls and DATA/EDATA/XDATA accesses.

AI8051U IP2/IP2H (B5/B6) are interrupt priority registers. The polling harness
keeps EA clear, so these tests alter priority bits without enabling interrupts.
The workload is independent of SDK algorithms; host C supplies the reference.
"""
import argparse
from pathlib import Path

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    header = '''#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#define ABI(n) __reentrant __sdcccall(n)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ABI(n)
#define __data
#define __edata
#define __xdata
#endif
u32 mix(u32 x) ABI(4);
u32 stress_run(u8 kind,u8 seed);
'''
    for kind in range(16):
        header += f'u32 work_{kind}(u8 seed) ABI({2 if kind<8 else 4});\n'
    (root/'stress.h').write_text(header)
    (root/'mix.c').write_text('#include "stress.h"\nu32 mix(u32 x) ABI(4) {return (x>>13)^(x<<5)^0x75923ad1UL;}\n')
    source = '''#include "stress.h"
#ifdef __SDCC
__sfr __at(0xb5) low;
__sfr __at(0xb6) high;
#else
static volatile u8 low,high;
#endif
static volatile __edata u8 ed[8];
static volatile __xdata u8 xd[8];
static volatile __data u8 cell;
'''
    for kind in range(16):
        bit = kind % 8
        source += f'''u32 work_{kind}(u8 seed) ABI({2 if kind<8 else 4}) {{
u32 a=0x12345678UL+seed,b=0x87654321UL^seed,c=0x13579bdfUL,d=0x2468ace0UL;
volatile u16 slots[3];u8 i;
low=seed;high=seed^0xa5;
for(i=0;i<4;++i) {{
slots[0]=(u16)a;slots[1]=(u16)b;slots[2]=seed+i;
ed[i]=seed+i;xd[i]=seed^(i*7);cell=i;
'''
        for at, name, op, expression in [(0xb5,'low','setb',f'|= {1<<bit}'),
                                        (0xb6,'high','cpl',f'^= {1<<bit}'),
                                        (0xb5,'low','clr',f'&= {255^(1<<bit)}')]:
            source += f'''#ifdef __SDCC
__asm {op} 0x{at:02x}.{bit} __endasm;
#else
{name} {expression};
#endif
'''
            source += f'''a=mix(a+{name}+slots[2]);
b+=ed[i]+slots[1]+cell;
c^=((u32)xd[i]<<16)|slots[0];
d=(d<<3)^(d>>5)^b^{name};
'''
        source += 'a^=c+d;\n}\nreturn a^b^c^d^((u32)high<<8)^low;\n}\n'
    (root/'work.c').write_text(source)
    source = '#include "stress.h"\nu32 stress_run(u8 kind,u8 seed) {switch(kind) {\n'
    source += ''.join(f'case {kind}:return work_{kind}(seed);\n' for kind in range(16))
    (root/'dispatch.c').write_text(source+'default:return 0;}}\n')


def reference(root, gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\nint main(void) { '
        'unsigned k,s;for(k=0;k<16;++k)for(s=0;s<64;++s)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall',*sorted(p.name for p in root.glob('*.c')),
                    '-o','reference.exe'],root,label='compile')
    values = {k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'],root).splitlines())}
    for bit in range(8):
        for seed in range(64):
            assert values[f'{bit}:{seed}'] == values[f'{bit+8}:{seed}']
    board.write(root/'reference.json',values)


def create_board(root):
    generate(root)
    source = (user_stress.SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end = source.index('        x0 = '),source.index('        TH0 = ')
    source = source[:start]+source[end:]
    start,end = source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source = source[:start]+'        result = stress_run(kind,input);\n'+source[end:]
    source = source.replace('kind < 2','kind < 16').replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files = sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    units = [dict(name='u'+str(i),file=n,backend='ordinary-wide',stackAuto=True,defines=[])
             for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],
        files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(peripheral_bits NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command',required=True)
    q = sub.add_parser('reference')
    q.add_argument('--output',type=Path,required=True)
    q.add_argument('--gcc',type=Path,required=True)
    q = sub.add_parser('board');q.add_argument('--output',type=Path,required=True)
    q = sub.add_parser('capture')
    for name in ('output','hex','reference'):
        q.add_argument('--'+name,type=Path,required=True)
    a = p.parse_args()
    if a.command == 'reference': reference(a.output,a.gcc)
    elif a.command == 'board': create_board(a.output)
    else: bitfield_fixture.capture(a.hex,a.output,a.reference)
