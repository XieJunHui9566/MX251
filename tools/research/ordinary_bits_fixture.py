"""Ordinary peripheral bits: readback, live scalars, volatile memory and ABI4 calls."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board

def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    a=0x12345678+seed; b=0x87654321^seed
    spi=0
    for i in range(8):
        spi=(spi|128)^8
        observed=(8<<8)|spi
        x=(a+seed+i+observed)&0xffffffff
        a=((x<<3)^(x>>5)^0x9e3779b9)&0xffffffff
        if seed&1:spi^=4
        else:spi&=~4
        b=(b+0x800000+spi)&0xffffffff
        a^=b
    return (a^b)&0xffffffff

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ['machine-board.h','machine-board.c','machine-control.c']:
        shutil.copy2(tests/name,output/name)
    (output/'bits.h').write_text('''#include "machine-board.h"
u32 bits_mix(u32 x) __sdcccall(4);
u32 bits_work(u8 seed);
''')
    (output/'mix.c').write_text('''#include "bits.h"
u32 bits_mix(u32 x) __sdcccall(4) { return (x<<3)^(x>>5)^0x9e3779b9UL; }
''')
    (output/'bits.c').write_text('''#include "bits.h"
__sfr __at(0xaf) IE2;
__sfr __at(0x9a) S2CON;
__sfr __at(0xce) SPCTL;
volatile __data u8 cell;
u32 bits_work(u8 seed) {
    u32 a=0x12345678UL+seed,b=0x87654321UL^seed,observed;
    u8 i;
    IE2=0; S2CON=0; SPCTL=0;
    for(i=0;i<8;++i) {
        cell=seed+i;
        __asm
            clr 0xaf.7
            setb 0x9a.3
            setb 0xce.7
            cpl 0xce.3
        __endasm;
        observed=((u32)IE2<<16)|((u16)S2CON<<8)|SPCTL;
        a=bits_mix(a+cell+observed);
        __asm
            setb 0xaf.7
            setb 0xaf.7
            cpl 0x9a.3
        __endasm;
        if(seed&1) { __asm cpl 0xce.2 __endasm; }
        else { __asm clr 0xce.2 __endasm; }
        b+=((u32)IE2<<16)|((u16)S2CON<<8)|SPCTL;
        __asm clr 0xaf.7 __endasm;
        a^=b;
    }
    return a^b;
}
''')
    p=output/'machine-board.c'
    text=p.read_text().replace('#include "machine-board.h"','#include "bits.h"\nu16 POINT_COLOR;u32 trace;')
    text=text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;','result = bits_work(input);')
    p.write_text(text)
    files=['machine-board.h','bits.h','bits.c','mix.c','machine-control.c','machine-board.c']
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='machine-board.c' else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(files[2:])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True)
    c=s.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=s.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
