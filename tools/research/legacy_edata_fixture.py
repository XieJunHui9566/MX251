"""EDATA scalar/alias traffic and software-triggered timer ISR equivalence."""
import argparse
from pathlib import Path

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    (root/'stress.h').write_text('''#ifdef __SDCC
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
#define __edata
#endif
typedef struct {u8 tag;u16 count;u32 total;u8 bytes[5];} State;
extern volatile __edata State state;
extern volatile __edata u16 word;
extern volatile __edata u32 value;
extern volatile __edata u8 done;
u32 read_value(void) ABI(2);
void write_value(u32 x) ABI(4);
void direct_step(void);
void alias_step(void);
void wide_step(void);
void interrupt_step(void);
u32 stress_run(u8 kind,u8 seed);
''')
    step = '''word=(u16)(word+0xfed3u);
value+=0x9e3779b9UL^word;
value^=0x1234abcdUL;
state.count=(u16)(state.count+word);
state.total+=value^state.count;
state.bytes[state.tag%5]^=(u8)word;
state.tag++;
'''
    (root/'direct.c').write_text('''#include "stress.h"
volatile __edata State state;
volatile __edata u16 word;
volatile __edata u32 value=0x98765432UL;
volatile __edata u8 done;
u32 read_value(void) ABI(2) {return value;}
void write_value(u32 x) ABI(4) {value=x;}
void direct_step(void) {'''+step+'}\n')
    alias = step.replace('state.', 'p->').replace('word', '(*w)').replace('value', '(*v)')
    (root/'alias.c').write_text('''#include "stress.h"
static volatile State * __edata saved;
void alias_step(void) {
volatile State *p;volatile u16 *w=&word;volatile u32 *v=&value;
saved=&state;p=saved;
'''+alias+'}\n')
    (root/'wide.c').write_text('#include "stress.h"\nvoid wide_step(void) {'+step+'}\n')
    (root/'interrupt.c').write_text('''#include "stress.h"
#ifdef __SDCC
__sbit __at(0xaf) ea;
__sbit __at(0xa9) et0;
__sbit __at(0x8d) tf0;
void timer(void) __interrupt(1) {
tf0=0;
#else
void timer(void) {
#endif
'''+step+'''done=1;
}
void interrupt_step(void) {
done=0;
#ifdef __SDCC
et0=1;tf0=1;ea=1;
while(!done) {}
ea=0;et0=0;
#else
timer();
#endif
}
''')
    (root/'work.c').write_text('''#include "stress.h"
u32 stress_run(u8 kind,u8 seed) {
u8 i;u32 sum=0x12345678UL,a=0xfedcba98UL+seed,b=0x13579bdfUL^seed;
state.tag=seed;state.count=65530u-seed;state.total=0xfedcba98UL;
for(i=0;i<5;i++)state.bytes[i]=seed+i;
word=65535u-seed;write_value(0x98765432UL+seed);
for(i=0;i<32;i++) {
if(kind==0)direct_step();else if(kind==1)interrupt_step();
else if(kind==2)alias_step();else wide_step();
/* Live locals across each interrupt and both register ABIs. */
a=(a<<5)^(a>>7)^read_value();b+=a^state.total;
sum^=a+b+state.bytes[i%5];
write_value(read_value()+sum);
}
return sum^a^b^state.total^state.count^word;
}
''')


def reference(root, gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\nint main(void) {'
        'unsigned k,s;for(k=0;k<4;k++)for(s=0;s<64;s++)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall',*sorted(p.name for p in root.glob('*.c')),
                    '-o','reference.exe'],root,label='compile')
    values={k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'],root).splitlines())}
    for kind in range(1,4):
        for seed in range(64):assert values[f'{kind}:{seed}']==values[f'0:{seed}']
    board.write(root/'reference.json',values)


def create_board(root):
    generate(root)
    source=(user_stress.SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end=source.index('        x0 = '),source.index('        checksum = 0;')
    # Timer 0 is explicitly triggered by the workload; this is a correctness
    # fixture and does not report execution timing.
    source=source[:start]+'        result = stress_run(kind,input);ticks=0;overflow=0;\n'+source[end:]
    source=source.replace('kind < 2','kind < 4').replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files=sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    units=[dict(name='u'+str(i),file=n,backend='ordinary-wide' if n=='wide.c' else 'legacy',
                stackAuto=True,defines=[]) for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],
        files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(legacy_edata NONE)\n'
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
