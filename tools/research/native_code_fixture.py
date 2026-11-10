"""Native CODE reads: byte/word/long, odd addresses, page crossings and ABI2 pointers."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board

BYTES=[(i*73+(i>>3)*11+29)&255 for i in range(520)]
WORDS=[(i*977+0x8123)&65535 for i in range(256)]
LONGS=[(i*0x192837+0x89abcdef)&0xffffffff for i in range(128)]

def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    result=0x13579bdf
    for k in range(8):
        i=(seed*37+k*19)&255
        odd=2*i+1
        values=[BYTES[i],WORDS[i],LONGS[i&127],int.from_bytes(bytes(BYTES[odd:odd+4]),'big'),
                int.from_bytes(bytes(BYTES[i:i+4]),'big'),BYTES[i+3],(i*4)&255]
        for value in values:result=((result<<3)^result^value)&0xffffffff
    return result

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ['machine-board.h','machine-board.c','machine-control.c']:shutil.copy2(tests/name,output/name)
    header='''#include "machine-board.h"
extern const __code u8 bytes[520];
extern const __code u16 words[256];
extern const __code u32 longs[128];
u8 code_byte(u8 i);
u16 code_word(u8 i);
u32 code_long(u8 i);
u32 code_odd(u16 i);
u32 code_volatile(u16 i);
u8 code_pointer(const u8 __code *p);
u8 code_scale(u8 i);
u32 code_case(u8 seed);
'''
    (output/'code.h').write_text(header)
    tables='#include "code.h"\n'
    for typ,name,values in [('u8','bytes',BYTES),('u16','words',WORDS),('u32','longs',LONGS)]:
        tables+='const __code '+typ+' '+name+'['+str(len(values))+']={'+','.join(hex(v)+('UL' if typ=='u32' else '') for v in values)+'};\n'
    (output/'tables.c').write_text(tables)
    (output/'loads.c').write_text('''#include "code.h"
u8 code_byte(u8 i) {return bytes[i];}
u16 code_word(u8 i) {return words[i];}
u32 code_long(u8 i) {return longs[i&127];}
u32 code_odd(u16 i) {return *(const u32 __code *)(bytes+i);}
u32 code_volatile(u16 i) {return *(const volatile u32 __code *)(bytes+i);}
u8 code_pointer(const u8 __code *p) {return p[3];}
u8 code_scale(u8 i) {return i*4;}
''')
    (output/'case.c').write_text('''#include "code.h"
#define MIX(v) result=(result<<3)^result^(v)
u32 code_case(u8 seed) {
    u8 k,i;u32 result=0x13579bdfUL;
    for(k=0;k<8;++k) {
        i=seed*37+k*19;
        MIX(code_byte(i)); MIX(code_word(i)); MIX(code_long(i));
        MIX(code_odd((u16)i*2+1)); MIX(code_volatile(i));
        MIX(code_pointer(bytes+i)); MIX(code_scale(i));
    }
    return result;
}
''')
    p=output/'machine-board.c'
    text=p.read_text().replace('#include "machine-board.h"','#include "code.h"\nu16 POINT_COLOR;u32 trace;')
    text=text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;','result = code_case(input);')
    p.write_text(text)
    files=['machine-board.h','code.h','tables.c','loads.c','case.c','machine-control.c','machine-board.c']
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
