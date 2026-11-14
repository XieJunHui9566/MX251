"""Live values across ABI4 argument widths, return values and timer interrupts."""
import argparse
from pathlib import Path
import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    (root/'stress.h').write_text('''#ifdef __SDCC
typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI(n) __reentrant __sdcccall(n)
#define PTR(x) ((u8 __xdata *)(x))
#define BITS(x) ((u32)(x))
#else
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define ABI(n)
#define __edata
#define __xdata
#define PTR(x) ((u8 *)(uintptr_t)(x))
#define BITS(x) ((u32)(uintptr_t)(x))
#endif
extern volatile __edata u8 cells[4],done;
extern volatile __edata u8 touch0,touch1;
extern volatile __edata u32 observed,irq_result;
void tap_byte(u8 a,u8 b) ABI(4);
u8 probe_old(void) ABI(2);u8 probe_native(void) ABI(4);u8 probe_wide(void) ABI(2);
void tap8(u8 a,u8 b) ABI(4);
void tap16(u16 a,u16 b) ABI(4);
void tap24(u8 __xdata *a,u8 __xdata *b) ABI(4);
void tap32(u32 a,u32 b) ABI(4);
u16 tap_stack(u8 a,u8 b,u16 c) ABI(4);
u32 step_old(void) ABI(2);u32 step_native(void) ABI(4);u32 step_wide(void) ABI(2);
u32 step_irq(void);u32 stress_run(u8 kind,u8 seed);
''')
    (root/'callees.c').write_text('''#include "stress.h"
volatile __edata u8 cells[4],done;
volatile __edata u8 touch0,touch1;
volatile __edata u32 observed,irq_result;
void tap_byte(u8 a,u8 b) ABI(4) {touch0=a;touch1=b;}
void tap8(u8 a,u8 b) ABI(4) {observed=((u32)a<<8)|b;}
void tap16(u16 a,u16 b) ABI(4) {observed=((u32)a<<16)|b;}
void tap24(u8 __xdata *a,u8 __xdata *b) ABI(4) {observed=BITS(a)^BITS(b);}
void tap32(u32 a,u32 b) ABI(4) {observed=a^b;}
u16 tap_stack(u8 a,u8 b,u16 c) ABI(4) {observed=((u32)a<<24)^((u32)b<<8)^c;return c^0x1357u;}
''')
    step='''u8 w=cells[0],x=cells[1],y=cells[2],z=cells[3];
u16 result;u32 sum=0x12345678UL;
tap8(w,x);sum+=observed+((u16)y<<8)+z;
tap8(x,w);sum^=observed+((u16)z<<8)+y;
tap16((u16)(0xff00u|w),(u16)(0x00ffu^((u16)x<<8)));
sum+=observed+w+x+y+z;
tap24(PTR(0x123400UL+w),PTR(0x234500UL+x));sum^=observed+((u16)y<<8)+z;
tap32(0x98760000UL+sum,0xabcde000UL+w+x);sum+=observed+y+z;
result=tap_stack(w,x,(u16)sum);sum^=observed+result+y+z;
cells[0]=(u8)(w+3);cells[1]=(u8)(x^y);cells[2]=(u8)(y+z);cells[3]=(u8)(z+w);
'''
    for label,abi in [('old',2),('native',4),('wide',2)]:
        probe=('u8 probe_'+label+'(void) ABI('+str(abi)+') {'
               'u8 w=cells[0],x=cells[1],y=cells[2],z=cells[3];'
               'tap_byte(w,x);return w^x^y^z;}\n')
        (root/(label+'.c')).write_text('#include "stress.h"\n'+probe+
            'u32 step_'+label+'(void) ABI('+str(abi)+') {u8 marker=probe_'+label+'();'+
            step+'return sum^marker^touch0^touch1;}\n')
    (root/'interrupt.c').write_text('''#include "stress.h"
#ifdef __SDCC
__sbit __at(0xaf) ea;__sbit __at(0xa9) et0;__sbit __at(0x8d) tf0;
void timer(void) __interrupt(1) {
#else
void timer(void) {
#endif
u8 marker=probe_old();
'''+step+'''irq_result=sum^marker^touch0^touch1;done=1;
#ifdef __SDCC
tf0=0;
#endif
}
u32 step_irq(void) {
done=0;
#ifdef __SDCC
et0=1;tf0=1;ea=1;
while(!done) {}
ea=0;et0=0;
#else
timer();
#endif
return irq_result;
}
''')
    (root/'work.c').write_text('''#include "stress.h"
u32 stress_run(u8 kind,u8 seed) {
u8 i;u32 sum=0x13579bdfUL+seed,r;
for(i=0;i<4;i++)cells[i]=(u8)(seed+19*i);observed=0;
for(i=0;i<8;i++) {
if(kind==0)r=step_old();else if(kind==1)r=step_native();
else if(kind==2)r=step_wide();else r=step_irq();
sum=(sum<<3)^(sum>>5)^r^observed;
}
return sum;
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
    source=source[:start]+'        result = stress_run(kind,input);ticks=0;overflow=0;\n'+source[end:]
    source=source.replace('kind < 2','kind < 4').replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files=sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    # Explicit-wide keeps ABI2 callers on legacy while enabling actual call
    # feedback. A project unit explicitly marked legacy receives no feedback.
    units=[dict(name='u'+str(i),file=n,backend='ordinary-wide' if n in ('wide.c','callees.c') else 'explicit-wide',
        stackAuto=True,defines=[]) for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(legacy_abi4 NONE)\n'
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
