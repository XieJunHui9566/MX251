"""Independent 9..32-bit field semantics, storage layout and mixed-backend ABI tests."""
import argparse
import json
import os
from pathlib import Path
import re

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress

WIDTHS = range(9, 33)
MIXED = (12, 16, 17, 24, 31, 32)
COUNT = len(WIDTHS) + len(MIXED)


def structure(bits):
    tail = (-bits) % 8
    base = 'int' if bits <= 16 else 'long'
    fields = ['u8 prefix;']
    for name, sign in [('value', 'unsigned'), ('bias', 'signed')]:
        fields.append(f'{sign} {base} {name}:{bits};')
        if tail:
            fields.append(f'unsigned char {name}_tail:{tail};')
        fields.append(f'u8 {name}_guard;')
    return f'typedef struct {{ {" ".join(fields)} }} F{bits};\n'


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    header = '''#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef signed long s32;
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
#define ABI
#define __data
#define __edata
#define __xdata
#define __code
#endif
'''
    header += ''.join(structure(n) for n in WIDTHS)
    for n in MIXED:
        for backend in ('wide', 'legacy'):
            header += f'void {backend}_set{n}(volatile F{n} *p,u32 v) ABI;\n'
            header += f'u32 {backend}_get{n}(const volatile F{n} *p) ABI;\n'
    header += ''.join(f'u32 layout_{n}(u8 seed);\n' for n in WIDTHS)
    header += ''.join(f'u32 mixed_{n}(u8 seed);\n' for n in MIXED)
    header += 'u32 stress_run(u8 kind,u8 seed);\n'
    (root/'stress.h').write_text(header)
    for n in WIDTHS:
        index = n-9
        mode = index % 4
        space = ('__edata', '__xdata', '__data', '')[mode]
        qualifier = 'volatile ' if index % 3 else ''
        count = 1 if mode == 2 else 2
        tail = (-n) % 8
        mask = (1 << n)-1
        signed_mask = (1 << (n-1))-1
        source = '#include "stress.h"\n'
        # CODE layout is checked by both the host semantic reference and the target.
        init = f'0x5a, {mask}UL,' + ('5,' if tail else '')
        init += '-1'  # value_guard, deliberately 255 after conversion
        init += ',-1,' + ('3,' if tail else '') + '0xa5'
        source += f'static const volatile F{n} __code rom = {{{init}}};\n'
        if mode != 3:
            source += f'static {qualifier}{space} F{n} state[{count}];\n'
        source += f'u32 layout_{n}(u8 seed) {{\n'
        if mode == 3:
            source += f'{qualifier}F{n} state[2];\n'
        if mode == 2:
            expr = 'state[0].'
        else:
            source += f'{qualifier}F{n} *p=&state[seed&1], *alias=p;\n'
            expr = 'alias->'
        source += 'u32 v,h=0x12345678UL; s32 b; u8 i;\n'
        source += f'{expr}prefix=seed;{expr}value_guard=0x5a;{expr}bias_guard=0xa5;\n'
        if tail:
            source += f'{expr}value_tail=seed;{expr}bias_tail=seed^0x55;\n'
        # Values cover zero, maximum, high-bit-only, mixed bytes and signed extremes.
        source += f'''for(i=0;i<4;++i) {{
switch((seed+i)&7) {{
case 0:v=0;break;case 1:v=0xffffffffUL;break;
case 2:v=1UL<<{n-1};break;case 3:v=0x80000000UL;break;
default:v=((u32)seed<<24)^((u32)(seed^0xa5)<<16)^((u32)i<<8)^0x5a;break;
}}
b=(s32)(v&{signed_mask}UL);
if((seed+i)&1)b=-b-1;
{expr}value=v;{expr}bias=b;
'''
        for field in ('value', 'bias', 'prefix', 'value_guard', 'bias_guard'):
            source += f'h=(h<<3)^h^(u32){expr}{field};\n'
        if tail:
            for field in ('value_tail', 'bias_tail'):
                source += f'h=(h<<3)^h^{expr}{field};\n'
        source += '}\n'
        # This checks the fixed MX251 ABI separately from GCC's own bitfield layout.
        # No raw byte assumptions are used by the host semantic hash.
        source += '#ifdef __SDCC\n{\n'
        address = '&state[0]' if mode == 2 else 'alias'
        bytes_ = (n+7)//8
        for member, val, offset in [('value', 'v', 1), ('bias', '(u32)b', bytes_+2)]:
            for k in range(bytes_):
                m = (1 << min(8, n-k*8))-1
                source += (f'if ((((const volatile u8 *){address})[{offset+k}] & {m}) != '
                           f'(({val} >> {8*k}) & {m}UL)) return 0xbad00000UL+{n*16+k};\n')
        source += '}\n#endif\n'
        source += 'h^=(u32)rom.value;h+=(u32)(s32)rom.bias;return h;}\n'
        (root/f'layout_{n}.c').write_text(source)
    for backend in ('wide', 'legacy'):
        source = '#include "stress.h"\n'
        for n in MIXED:
            mask = (1 << (n-1))-1
            source += f'''void {backend}_set{n}(volatile F{n} *p,u32 v) ABI {{
p->value=v; p->bias=-(s32)(v&{mask}UL)-1;
}}
u32 {backend}_get{n}(const volatile F{n} *p) ABI {{
return ((u32)p->value<<1)^(u32)(s32)p->bias;
}}
'''
        (root/(backend+'.c')).write_text(source)
    source = '#include "stress.h"\n'
    for n in MIXED:
        source += f'''static volatile __xdata F{n} mixed_state{n};
u32 mixed_{n}(u8 seed) {{
u32 v=((u32)seed<<24)^((u32)(seed^0x55)<<16)^0xa55aUL,h;
wide_set{n}(&mixed_state{n},v);h=legacy_get{n}(&mixed_state{n});
legacy_set{n}(&mixed_state{n},~v);return (h<<3)^wide_get{n}(&mixed_state{n});
}}
'''
    (root/'mixed.c').write_text(source)
    source = '#include "stress.h"\nu32 stress_run(u8 kind,u8 seed) {switch(kind) {\n'
    names = [f'layout_{n}' for n in WIDTHS]+[f'mixed_{n}' for n in MIXED]
    source += ''.join(f'case {i}:return {name}(seed);\n' for i, name in enumerate(names))
    (root/'dispatch.c').write_text(source+'default:return 0;}}\n')
    board.write(root/'source.json',dict(widths=list(WIDTHS),mixed=list(MIXED),
        generatorSha256=board.sha(Path(__file__)),
        files={p.name:board.sha(p) for p in sorted(root.iterdir()) if p.suffix in ('.c','.h')}))


def reference(root, gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\nint main(void) { '
        f'unsigned k,s;for(k=0;k<{COUNT};++k)for(s=0;s<64;++s)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall',*sorted(p.name for p in root.glob('*.c')),
                    '-o','reference.exe'],root,label='compile')
    values={k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'],root).splitlines())}
    board.write(root/'reference.json',values)


def create_board(root, abi):
    generate(root)
    source=(user_stress.SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:start]+source[end:]
    start,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:start]+'        result = stress_run(kind,input);\n'+source[end:]
    source=source.replace('kind < 2',f'kind < {COUNT}')
    source=source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files=sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    units=[dict(name='u'+str(i),file=n,backend='legacy' if n=='legacy.c' else 'ordinary-wide',
                stackAuto=True,defines=[]) for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[f'TEST_ABI={abi}'],
        files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(multibyte_fields NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


def transactions(asm):
    checked={}
    for name,body in re.findall(r'; mcs251 wide-mir accepted: ((?:access|signed)\d+);(.*?); mx251-function end',asm,re.S):
        bits=int(re.search(r'\d+',name)[0]);size=(bits+7)//8
        expected=['store']*(size-1)+(['load'] if bits%8 else [])+['store']+['load']*size
        actual=['store' if x.startswith('@') else 'load' for x in re.findall(
            r'^\s*mov\s+((?:a|r\d+),@dr16|@dr16,(?:a|r\d+))\s*$',body,re.M)]
        assert actual==expected,(name,actual,expected)
        assert not re.search(r'\bmov\s+(?:[wd]r\d+,@dr16|@dr16,[wd]r\d+)',body),name
        checked[name]=actual
    assert checked
    return checked


def contracts(root, sdcc, support):
    root.mkdir(parents=True, exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
    env['SDCC_HOME']=str(support);env['PATH']=str(support/'bin')+os.pathsep+env['PATH']
    # Keep each function small enough to isolate semantic admission from capacity.
    source='#include "stress.h"\n'
    for n in WIDTHS:
        source += f'''u32 access{n}(volatile F{n} *p,u32 v) ABI {{p->value=v;return p->value;}}
s32 signed{n}(volatile F{n} *p,s32 v) ABI {{p->bias=v;return p->bias;}}
u32 code{n}(const F{n} __code *p) ABI {{return p->value^(u32)(s32)p->bias;}}
'''
    rows=[]
    for abi in (2,4):
      for model in ('small','large'):
       for opt in ('size','speed'):
        d=root/f'abi{abi}-{model}-{opt}';generate(d)
        (d/'probe.c').write_text(source)
        flags=['-mmcs251','--std-sdcc11','--stack-auto','--mcs251-native-abi','--mcs251-machine-ir',
               '--mcs251-function-metadata','--mcs251-flat-code-reads','--mcs251-wide-ordinary',
               '--model-'+model,'--opt-code-'+opt,f'-DTEST_ABI={abi}']
        user_stress.run([sdcc,*flags,'-c','probe.c'],d,env,'probe')
        asm=(d/'probe.asm').read_text()
        refusals=re.findall(r'; wide (?:refusal|admission) function=(\S+).*',asm)
        assert not refusals,(d,refusals)
        rows.append(dict(abi=abi,model=model,optimization=opt,functions=72,refusals=refusals,
                         byteTransactions=transactions(asm)))
    board.write(root/'results.json',dict(compilerSha256=board.sha(sdcc),rows=rows))
    print('PASS: 8 configurations, 24 widths, signed/unsigned/CODE',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True)
    q=s.add_parser('reference');q.add_argument('--output',type=Path,required=True);q.add_argument('--gcc',type=Path,required=True)
    q=s.add_parser('board');q.add_argument('--output',type=Path,required=True);q.add_argument('--abi',type=int,choices=(2,4),required=True)
    q=s.add_parser('contracts')
    for n in ('output','sdcc','support'):q.add_argument('--'+n,type=Path,required=True)
    q=s.add_parser('capture')
    for n in ('output','hex','reference'):q.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    if a.command=='reference':reference(a.output,a.gcc)
    elif a.command=='board':create_board(a.output,a.abi)
    elif a.command=='contracts':contracts(a.output,a.sdcc,a.support)
    else:bitfield_fixture.capture(a.hex,a.output,a.reference)
