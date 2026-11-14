"""Independent C workloads for signed narrow products and discarded volatile reads."""
import argparse
from pathlib import Path

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    header = '''#ifdef __SDCC
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
#define ABI(n) __reentrant __sdcccall(n)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
#define ABI(n)
#define __data
#define __edata
#define __xdata
#define __code
#endif
u32 mix(u32 x) ABI(4);
u32 compact(u32 x) ABI(4);
u32 stress_run(u8 kind,u8 seed);
'''
    for abi in (2,4):
        header += f'''u16 product_{abi}(s8 a,s8 b) ABI({abi});
u8 low_{abi}(s8 a,s8 b) ABI({abi});
u16 mixed_{abi}(s8 a,u8 b) ABI({abi});
u8 digit_{abi}(u8 c) ABI({abi});
'''
    for kind in range(16):
        header += f'u32 work_{kind}(u8 seed) ABI({2 if kind<8 else 4});\n'
    (root/'stress.h').write_text(header)
    source='#include "stress.h"\nu32 mix(u32 x) ABI(4) {return (x>>13)^(x<<5)^0x75923ad1UL;}\n'
    for abi in (2,4):
        source+=f'''u16 product_{abi}(s8 a,s8 b) ABI({abi}) {{return a*b;}}
u8 low_{abi}(s8 a,s8 b) ABI({abi}) {{return a*b;}}
u16 mixed_{abi}(s8 a,u8 b) ABI({abi}) {{return a*b;}}
u8 digit_{abi}(u8 c) ABI({abi}) {{return c-'0';}}
'''
    (root/'arithmetic.c').write_text(source)
    (root/'compact.c').write_text('#include "stress.h"\nu32 compact(u32 x) ABI(4) {'
        'volatile u16 local=257;local;return (x>>13)+local;}\n')
    source='''#include "stress.h"
static volatile __data u16 cell;
static volatile __edata u32 extended;
static volatile __xdata u32 farcell=0x98765432UL;
static const volatile __code u16 rom=0x5719;
#ifdef __SDCC
__sfr __at(0xb5) port;
#else
static volatile u8 port;
#endif
'''
    for kind in range(16):
        abi=2 if kind<8 else 4
        mode=kind%8
        source+=f'u32 work_{kind}(u8 seed) ABI({abi}) {{\n'
        if mode<3:
            function=('product','low','mixed')[mode]
            # Each seed covers four distinct signed inputs and all 256 second
            # operands: the 64 seed checksums cover every 8x8 input pair.
            source+=f'''u16 j;u8 i;u32 result=0x12345678UL;
for(i=0;i<4;++i)for(j=0;j<256;++j) {{
s8 a=(s8)((s16)seed*4+i-128);
result=mix(result)^{function}_{abi}(a,{'(u8)j' if mode==2 else '(s8)((s16)j-128)'});
}}
return result; }}
'''
        elif mode==3:
            source+=f'''u8 text[6],i,value=0;
for(i=0;i<6;++i)text[i]='0'+((seed+i*7)%10);
for(i=0;i<6;++i){{if(digit_{abi}(text[i])>=10)break;value=value*10+digit_{abi}(text[i]);}}
return mix(value)+text[seed%6]; }}
'''
        else:
            reads=('cell;cell;','extended;farcell;','port|=255;port&=0;rom;','local;cell;extended;farcell;')[mode-4]
            source+=f'''u32 a=0x12345678UL+seed,b=0x98765432UL^seed,c=0x7fedcba9UL;
volatile u16 local=seed+257;u8 i;
cell=seed+259;extended=a;port=seed;
for(i=0;i<4;++i){{
{reads}
a=mix(a+cell+local);b=mix(b^extended);c+=compact(b)+local;
cell+=i;extended^=b;local+=seed;
}}
return a^b^c^port; }}
'''
    (root/'work.c').write_text(source)
    source='#include "stress.h"\nu32 stress_run(u8 kind,u8 seed) {switch(kind) {\n'
    source+=''.join(f'case {kind}:return work_{kind}(seed);\n' for kind in range(16))
    (root/'dispatch.c').write_text(source+'default:return 0;}}\n')


def reference(root,gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\nint main(void) { '
        'unsigned k,s;for(k=0;k<16;++k)for(s=0;s<64;++s)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall',*sorted(p.name for p in root.glob('*.c')),
                    '-o','reference.exe'],root,label='compile')
    values={k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'],root).splitlines())}
    for kind in range(8):
        for seed in range(64):
            assert values[f'{kind}:{seed}']==values[f'{kind+8}:{seed}']
    board.write(root/'reference.json',values)


def create_board(root):
    generate(root)
    source=(user_stress.SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:start]+source[end:]
    start,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:start]+'        result = stress_run(kind,input);\n'+source[end:]
    source=source.replace('kind < 2','kind < 16').replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files=sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    units=[dict(name='u'+str(i),file=n,backend='ordinary-wide',stackAuto=n!='compact.c',defines=[])
           for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],
        files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(scalar_semantics NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    q=sub.add_parser('reference');q.add_argument('--output',type=Path,required=True);q.add_argument('--gcc',type=Path,required=True)
    q=sub.add_parser('board');q.add_argument('--output',type=Path,required=True)
    q=sub.add_parser('capture')
    for name in ('output','hex','reference'):q.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.command=='reference':reference(a.output,a.gcc)
    elif a.command=='board':create_board(a.output)
    else:bitfield_fixture.capture(a.hex,a.output,a.reference)
