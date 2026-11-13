"""Poison C storage, re-enter startup, and check actual AI8051U initialization."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


STORAGE = r'''
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
volatile __data u8 dzero[3];
volatile __idata u8 izero[17];
volatile __bit bzero;
#if MIXED
volatile __xdata u8 xzero[257];
volatile __edata u8 ezero[260];
volatile __data u16 dinitial = 0x1357;
volatile __idata u8 iinitial[3] = {0x12, 0x34, 0x56};
volatile __edata u8 einitial[5] = {0x9b};
volatile __xdata u8 xinitial[5] = {0x12, 0x34, 0x56, 0x78, 0x9a};
volatile __edata u8 * __edata saved = &ezero[259];
volatile __bit binitial = 1;
#endif

u32 storage_check(void)
{
    u16 i;
    u32 failed = 0;
    for (i = 0; i < 3; ++i) if (dzero[i]) failed |= 1;
    for (i = 0; i < 17; ++i) if (izero[i]) failed |= 2;
    if (bzero) failed |= 4;
#if MIXED
    for (i = 0; i < 257; ++i) if (xzero[i]) failed |= 8;
    for (i = 0; i < 260; ++i) if (ezero[i]) failed |= 16;
    if (dinitial != 0x1357) failed |= 32;
    if (iinitial[0] != 0x12 || iinitial[1] != 0x34 || iinitial[2] != 0x56) failed |= 64;
    if (einitial[0] != 0x9b || einitial[1] || einitial[2] || einitial[3] || einitial[4]) failed |= 128;
    if (xinitial[0] != 0x12 || xinitial[1] != 0x34 || xinitial[2] != 0x56 || xinitial[3] != 0x78 || xinitial[4] != 0x9a) failed |= 256;
    if (saved != &ezero[259]) failed |= 512;
    if (!binitial) failed |= 1024;
#endif
    return failed;
}

void storage_poison(u8 seed)
{
    u16 i;
    u8 value = seed | 0x80;
    for (i = 0; i < 3; ++i) dzero[i] = value;
    for (i = 0; i < 17; ++i) izero[i] = value;
    bzero = 1;
#if MIXED
    for (i = 0; i < 257; ++i) xzero[i] = value;
    for (i = 0; i < 260; ++i) ezero[i] = value;
    dinitial = 0;
    for (i = 0; i < 3; ++i) iinitial[i] = value;
    for (i = 0; i < 5; ++i) { einitial[i] = value; xinitial[i] = value; }
    saved = 0;
    binitial = 0;
#endif
}
'''

MAIN = r'''
#include "machine-board.h"
SFR(P3,0xb0); SFR(P3M1,0xb1); SFR(P3M0,0xb2); SFR(P_SW1,0xa2);
SFR(AUXR,0x8e); SFR(SCON,0x98); SFR(SBUF,0x99);
SFR(T2H,0xd6); SFR(T2L,0xd7); SFR(WTST,0xe9); SFR(CKCON,0xea);
SBIT(EA,0xaf); SBIT(TI,0x99);
/* Absolute uninitialized objects are outside XSEG and survive re-entry. */
volatile __xdata __at(0x16ff0) u32 cookie;
volatile __xdata __at(0x16ff4) u8 cycle;
static u8 checksum;
u32 storage_check(void);
void storage_poison(u8 seed);
void _sdcc_gsinit_startup(void);

static void send(u8 value)
{
    u16 timeout = 65535;
    checksum ^= value; SBUF = value;
    while (!TI && --timeout) {}
    TI = 0;
}

void main(void)
{
    u8 seed, kind;
    u32 failed = storage_check();
    /* Locals and nested calls also exercise the restored SPX on every boot. */
    EA = 0;
    P3 |= 3; P3M1 &= 0xfc; P3M0 = (P3M0 & 0xfc) | 2;
    P_SW1 &= 0x3f; AUXR &= 0x67; SCON = 0x40;
    T2L = 0xa9; T2H = 0xff; AUXR |= 0x15;
    WTST = 0; CKCON = 0;
    if (cookie == 0x51c251a7UL) {
        seed = cycle & 63; kind = (cycle >> 6) & 1;
        checksum = 0;
        send('M'); send('I'); send('R'); send('1'); send(kind); send(seed);
        send(failed >> 24); send(failed >> 16); send(failed >> 8); send(failed);
        send(0); send(0); send(0); send(checksum); send(13); send(10);
        cycle = (cycle+1) & 127;
    } else cycle = 0;
    storage_poison(cycle);
    cookie = 0x51c251a7UL;
    _sdcc_gsinit_startup();
    for (;;) {}
}
'''


def create(output, mixed, model, hook, runtime='standard'):
    output.mkdir(parents=True, exist_ok=False)
    storage=STORAGE
    if runtime=='native-math':
        storage=storage.replace('u32 storage_check(void)',
            'volatile __edata float math_initial=1.25f, math_zero;\nu32 storage_check(void)')
        storage=storage.replace('    return failed;',
            '    if (math_initial!=1.25f || math_zero!=0.0f) failed |= 2048;\n'
            '    if (math_initial/5.0f!=0.25f) failed |= 4096;\n    return failed;')
        storage=storage.replace('    bzero = 1;',
            '    bzero = 1;\n    math_initial=7.0f; math_zero=9.0f;')
    (output/'storage.c').write_text(storage, encoding='ascii', newline='\n')
    (output/'board.c').write_text(MAIN, encoding='ascii', newline='\n')
    (output/'machine-board.h').write_text(
        'typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\n'
        '#define SFR(name,address) __sfr __at(address) name\n'
        '#define SBIT(name,address) __sbit __at(address) name\n', encoding='ascii')
    sources=['storage.c','board.c']
    if hook is not None:
        (output/'hook.c').write_text('unsigned char __sdcc_external_startup(void) { return '+str(hook)+'; }\n', encoding='ascii')
        sources.append('hook.c')
    files=sources+['machine-board.h']
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='board.c' and runtime=='standard' else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(sources)]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model=model,optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime=runtime,defines=['MIXED='+str(int(mixed))],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(linked_startup NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n',encoding='ascii')
    board.write(output/'source.json',dict(mixed=mixed,model=model,customHook=hook,runtime=runtime,files={f:board.sha(output/f) for f in files}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True);c.add_argument('--mixed',action='store_true');c.add_argument('--model',choices=['small','large'],default='small');c.add_argument('--hook',type=int,choices=[0,1]);c.add_argument('--runtime',choices=['standard','native-math'],default='standard')
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output,a.mixed,a.model,a.hook,a.runtime)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,lambda kind,seed:0))
