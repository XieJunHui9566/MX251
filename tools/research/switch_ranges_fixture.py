"""Shared MX251/Keil range-dispatch checks and an unchanged p095 function timer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

import machine_pipeline_fixture as board
from unified_sdk_matrix import build_keil, image, pin, read, save


def expected(kind, seed):
    if kind == 0:
        interface, parity = (seed//8) % 4, seed % 8
        mode = 0x50 if parity == 0 or parity > 4 else 0xd0 if parity == 4 else 0xd8
        return (mode if interface == 0 else 0x42) | ((mode if interface == 2 else 0x02) << 8)
    result = 0x13579bdf
    for j in range(4):
        key = seed*4+j
        value = (key*257+seed) & 65535
        a = (value+0x1234 if key == 0 else value^0x55aa if key in (1, 2, 3)
             else value-99 if key == 4 else value+7) & 65535
        b = (value+13 if key < 64 else value^0x1234 if key < 192 else value-21) & 65535
        signed = key if key < 128 else key-256
        c = (value+19 if signed == -2 else value^0x4321 if -1 <= signed <= 1
             else value-17 if signed == 2 else value+3) & 65535
        t = key % 7
        d = 0x1234 if t == 0 else 0xabcd if 1 <= t <= 3 else 0x5a5a if t == 4 else 0x9876
        result = ((result << 3)^result^a^(b << 16)^c^d) & 0xffffffff
    return result


def create(source, output):
    output.mkdir(parents=True, exist_ok=False)
    data = source.read_bytes()
    m = re.search(rb'void\s+uart_set_parity\s*\([^)]*\)\s*\{', data)
    start, end, depth = m.start(), m.end(), 1
    while depth:
        depth += (data[end] == 123)-(data[end] == 125)
        end += 1
    function = data[start:end]
    header = '''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifdef __SDCC
#define SFR(name,address) __sfr __at(address) name
#else
#define SFR(name,address) sfr name = address
#endif
SFR(SCON,0x98); SFR(S2CON,0x9a);
typedef struct { u32 dwDTERate; u8 bCharFormat,bParityType,bDataBits; } LINECODING;
extern u8 Interface;
extern LINECODING LineCoding1,LineCoding2;
#define NONE_PARITY 0
#define ODD_PARITY 1
#define EVEN_PARITY 2
#define MARK_PARITY 3
#define SPACE_PARITY 4
void uart_set_parity();
u32 range_work(u8);
'''
    (output/'ranges.h').write_text(header, newline='\n')
    (output/'parity.c').write_bytes(b'#include "ranges.h"\nu8 Interface;\nLINECODING LineCoding1,LineCoding2;\n'+function+b'\n')
    ranges = '''#include "ranges.h"
u16 range_small(u8 key,u16 v) {
 switch(key) { case 0:return v+0x1234u; case 1:case 2:case 3:return v^0x55aau;
 case 4:return v-99u; default:return v+7u; }
}
u16 range_signed(signed char key,u16 v) {
 switch(key) { case -2:return v+19u; case -1:case 0:case 1:return v^0x4321u;
 case 2:return v-17u; default:return v+3u; }
}
u16 range_full(u8 key,u16 v) { switch(key) {
'''
    for low, high, expression in ((0, 64, 'v+13u'), (64, 192, 'v^0x1234u'), (192, 256, 'v-21u')):
        ranges += ''.join(f'case {i}:' for i in range(low, high))+'return '+expression+';\n'
    ranges += '} return 0; }\n'
    ranges += '''u16 tail_a(void) { return 0x1234u; }
u16 tail_b(void) { return 0xabcdu; }
u16 tail_c(void) { return 0x5a5au; }
u16 tail_d(void) { return 0x9876u; }
u16 range_tail(u8 key) { switch(key) {
 case 0:return tail_a(); case 1:case 2:case 3:return tail_b();
 case 4:return tail_c(); default:return tail_d(); } }
'''
    (output/'ranges.c').write_text(ranges, newline='\n')
    work = '''#include "ranges.h"
u16 range_small(u8,u16);
u16 range_signed(signed char,u16);
u16 range_full(u8,u16);
u16 range_tail(u8);
u32 range_work(u8 seed) {
 u8 j,key; u16 v,a,b,c,d; u32 result=0x13579bdfUL;
 for(j=0;j<4;++j) {
  key=seed*4+j; v=(u16)key*257u+seed;
  a=range_small(key,v); b=range_full(key,v); c=range_signed((signed char)key,v); d=range_tail(key%7);
  result=(result<<3)^result^a^((u32)b<<16)^c^d;
 }
 return result;
}
'''
    (output/'work.c').write_text(work, newline='\n')
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    main = (tests/'machine-board.c').read_text().replace('#include "machine-board.h"', '#include "ranges.h"')
    main = main.replace('SFR(AUXR,0x8e); SFR(SCON,0x98); SFR(SBUF,0x99);', 'SFR(AUXR,0x8e); SFR(SBUF,0x99);')
    main = main.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks; u8 repeat;')
    begin, end = main.index('        x0 ='), main.index('        TH0 =')
    main = main[:begin]+'''        Interface=(input/8)%4;
        LineCoding1.bParityType=LineCoding2.bParityType=input%8;
        SCON=0x42; S2CON=0x02;
'''+main[end:]
    begin, end = main.index('        if (kind == 0)'), main.index('        TR0 = 0; ticks')
    main = main[:begin]+'''        if(kind==0) {
            for(repeat=0;repeat<32;++repeat) uart_set_parity();
            result=(u16)SCON | ((u16)S2CON<<8);
        } else result=range_work(input);
'''+main[end:]
    main = main.replace('        checksum = 0;', '        SCON=0x40;\n        checksum = 0;')
    (output/'board.c').write_text(main, newline='\n')
    files = ['ranges.h', 'ranges.c', 'parity.c', 'work.c', 'board.c']
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f == 'board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[1:])]
    save(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
         backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard', defines=[],
         files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup']))
    save(output/'source.json', dict(extractedSource=pin(source), function='uart_set_parity',
         functionSha256=hashlib.sha256(function).hexdigest(), functionChanged=False,
         timingScope='32 calls plus loop and result capture; not full p095 execution time', files=[pin(output/f) for f in files]))


def keil(source, config, output):
    output.mkdir(parents=True, exist_ok=False)
    native = output/'native'; native.mkdir()
    for p in source.iterdir():
        if p.suffix in ('.h', '.c'):shutil.copy2(p, native/p.name)
    save(output/'original-inputs.json', dict(sources=['ranges.c', 'parity.c', 'work.c', 'board.c'], assembly=[],
         options='<Target251><Target251Misc><uFrame4>0</uFrame4></Target251Misc></Target251>', defines='', optLevel=7))
    out=output/'keil';out.mkdir()
    result=build_keil(read(config), dict(id='switch-fixture', historicalKeilBytes=None), out)
    save(out/'result.json',result)
    print(json.dumps(result))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--source',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('keil');c.add_argument('--source',type=Path,required=True);c.add_argument('--config',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.source,a.output)
    elif a.command=='keil':keil(a.source,a.config,a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
