"""Portable boolean semantics, equivalent user code and cross-backend ABI tests."""
import argparse
import json
import os
from pathlib import Path
import re

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


CASES = ['byte', 'word', 'dword', 'signed', 'generic_pointer', 'code_pointer',
         'function_pointer', 'data', 'edata', 'xdata', 'local', 'record',
         'array', 'code_array', 'wide_first', 'wide_second', 'wide_third',
         'wide_return', 'legacy_first', 'legacy_second', 'legacy_third',
         'legacy_return', 'legacy_invoke', 'branch_clean', 'branch_messy',
         'state_clean', 'state_messy', 'parser_clean', 'parser_messy', 'updates']


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    header = '''#ifndef BOOLEAN_FIXTURE_H
#define BOOLEAN_FIXTURE_H
#ifdef __SDCC
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#define PTR(T,V) ((T *)(V))
#else
#include <stdint.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#define __data
#define __edata
#define __xdata
#define __code
#define ABI
#define PTR(T,V) ((T *)(uintptr_t)(V))
#endif
typedef u8 (*Callback)(u8) ABI;
u32 input(u8 seed) ABI;
u8 target(u8 x) ABI;
u32 stress_run(u8 kind,u8 seed);
'''
    for prefix in ('wide', 'legacy'):
        header += f'''u32 {prefix}_first(_Bool b,u32 x,u8 y) ABI;
u32 {prefix}_second(u32 x,_Bool b,u8 y) ABI;
u32 {prefix}_third(u32 x,u8 y,_Bool b) ABI;
_Bool {prefix}_truth(u32 x) ABI;
'''
    header += 'u32 legacy_invoke(u32 x,u8 y) ABI;\n#endif\n'
    (root/'stress.h').write_text(header)
    (root/'input.c').write_text('''#include "stress.h"
static const u32 __code inputs[16]={0,1,2,0x80,0x100,0x8000,0x10000,0x80000000UL,
    0xffffffffUL,0xffff0000UL,0x01000000UL,0x00800000UL,0x10001UL,0x101UL,0x7fffffffUL,0};
u32 input(u8 seed) ABI {return inputs[seed&15];}
u8 target(u8 x) ABI {return x+1;}
''')
    for prefix in ('wide', 'legacy'):
        source = '#include "stress.h"\n'
        for position, args in [('first','_Bool b,u32 x,u8 y'),
                               ('second','u32 x,_Bool b,u8 y'),
                               ('third','u32 x,u8 y,_Bool b')]:
            source += f'u32 {prefix}_{position}({args}) ABI {{return x+((u32)b<<16)+y;}}\n'
        source += f'_Bool {prefix}_truth(u32 x) ABI {{return x;}}\n'
        if prefix == 'legacy':
            source += 'u32 legacy_invoke(u32 x,u8 y) ABI {return wide_third(x,y,x);}\n'
        (root/(prefix+'.c')).write_text(source)
    source = '''#include "stress.h"
static volatile _Bool __data db;
static volatile _Bool __edata eb;
static volatile _Bool __xdata xb;
struct Record {_Bool ready; u8 guard; _Bool active; u8 end;};
static struct Record __data records[2];
static volatile _Bool __xdata flags[4];
static const _Bool __code ct[4]={0,1,1,0};
'''
    for name in CASES:
        body = 'u32 x=input(seed); '
        if name in ('byte', 'word', 'dword'):
            typ = {'byte':'u8','word':'u16','dword':'u32'}[name]
            body += f'_Bool a=({typ})x; return (u32)a+((u32)(!a)<<8)+((u32)(_Bool)(_Bool)a<<16);'
        elif name == 'signed':
            body += ('s8 a=-(s8)(seed&127); s16 b=-(s16)((u16)seed<<8); '
                     's32 c=-(s32)((u32)seed<<23); '
                     'return (u32)(_Bool)a+((u32)(_Bool)b<<8)+((u32)(_Bool)c<<16);')
        elif name in ('generic_pointer','code_pointer'):
            typ = 'const u8 __code' if name == 'code_pointer' else 'u8'
            body += f'{typ} *p=PTR({typ},x&0xffffffUL); _Bool b=p; return (u32)b+((u32)(!p)<<8);'
        elif name == 'function_pointer':
            body += 'Callback p=(seed&1)?target:(Callback)0; _Bool b=p; return (u32)b+((u32)(!p)<<8);'
        elif name in ('data','edata','xdata'):
            obj = {'data':'db','edata':'eb','xdata':'xb'}[name]
            body += f'{obj}=x; x=(u32){obj}; {obj}=!{obj}; return x+((u32){obj}<<8);'
        elif name == 'local':
            body += 'volatile _Bool a=x; volatile _Bool *p=&a; x=(u32)*p; *p=!a; return x+((u32)a<<8);'
        elif name == 'record':
            body += ('struct Record __data *p=&records[seed&1]; p->guard=0x5a; p->end=0xa5; '
                     'p->ready=x; p->active=!p->ready; return (u32)p->ready+((u32)p->active<<8)'
                     '+((u32)p->guard<<16)+((u32)p->end<<24);')
        elif name == 'array':
            body += 'u8 i=seed&3; flags[i]=x; x=(u32)flags[i]; flags[i]=!flags[i]; return x+((u32)flags[i]<<8);'
        elif name == 'code_array':
            body = 'return (u32)ct[seed&3];'
        elif name.endswith('_return'):
            body += f'return (u32){name.split("_")[0]}_truth(x);'
        elif name == 'legacy_invoke':
            body += 'return legacy_invoke(x,seed);'
        elif name.startswith(('wide_', 'legacy_')):
            args = {'first':'x,x,seed','second':'x,x,seed','third':'x,seed,x'}[name.split('_')[1]]
            body += f'return {name}({args});'
        elif name == 'branch_clean':
            body += 'if(x) return (u32)seed+17; return (u32)seed+3;'
        elif name == 'branch_messy':
            body += '_Bool a=x; u8 b=a; if(!(_Bool)b) goto empty; return (u32)seed+17; empty:return (u32)seed+3;'
        elif name.startswith('state_'):
            body = 'u8 i; u16 pending=seed; u32 sum=0; '
            body += 'for(i=0;i<12;++i) { pending=(pending<<1)^(u16)(0x100u+i); '
            if name.endswith('clean'):
                body += 'if(pending && (i&1)) sum+=(u32)pending; else sum^=i; '
            else:
                body += ('_Bool ready=pending; _Bool enabled=i&1; u8 done=0; '
                         'if(ready) {if(enabled) {sum+=(u32)pending;done=1;}} if(!done) sum^=i; ')
            body += '} return sum;'
        elif name.startswith('parser_'):
            body = 'u8 i,seen=0; u16 length=0; u32 sum=0; '
            body += 'for(i=0;i<16;++i) {u16 v=(u16)(seed+i)<<8; '
            if name.endswith('clean'):
                body += 'if(v && (++seen&1)) {length+=v; sum^=length;} '
            else:
                body += ('_Bool valid=v; _Bool accept=0; if(valid) {++seen; accept=seen&1;} '
                         'if(accept) {length+=v; sum^=length;} ')
            body += '} return sum^((u32)seen<<24);'
        elif name == 'updates':
            body += ('_Bool b=x; u32 r=b; r|=(u32)b++<<4; r|=(u32)b<<8; '
                     'b=0; b+=x; r|=(u32)b<<12; b^=1; return r|((u32)b<<16);')
        source += f'u32 test_{name}(u8 seed) {{ {body} }}\n'
    source += 'u32 stress_run(u8 kind,u8 seed) {switch(kind) {\n'
    source += ''.join(f'case {k}: return test_{n}(seed);\n' for k,n in enumerate(CASES))
    source += 'default:return 0;} }\n'
    (root/'cases.c').write_text(source)
    board.write(root/'source.json',dict(cases=CASES,generatorSha256=board.sha(Path(__file__)),
        files={p.name:board.sha(p) for p in sorted(root.iterdir()) if p.suffix in ('.c','.h')}))


def reference(root,gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\n'
        f'int main(void){{unsigned k,s; for(k=0;k<{len(CASES)};++k) for(s=0;s<64;++s) '
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall',*sorted(p.name for p in root.glob('*.c')),
                     '-o','reference.exe'],root,label='compile')
    values={k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'],root).splitlines())}
    for names in [('branch_clean','branch_messy'),('state_clean','state_messy'),
                  ('parser_clean','parser_messy'),
                  ('wide_first','wide_second','wide_third','legacy_first','legacy_second','legacy_third','legacy_invoke'),
                  ('wide_return','legacy_return')]:
        for s in range(64):
            expected=values[f'{CASES.index(names[0])}:{s}']
            assert all(values[f'{CASES.index(n)}:{s}']==expected for n in names),(names,s)
    board.write(root/'reference.json',values)


def create_board(root,abi):
    generate(root)
    source=(user_stress.SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:start]+source[end:]
    start,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:start]+'        result = stress_run(kind,input);\n'+source[end:]
    source=source.replace('kind < 2',f'kind < {len(CASES)}')
    source=source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files=sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    units=[dict(name='u'+str(i),file=n,backend='legacy' if n=='legacy.c' else 'ordinary-wide',
                stackAuto=True,defines=[]) for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[f'TEST_ABI={abi}'],
        files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(booleans NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


def contracts(root,sdcc,support):
    root.mkdir(parents=True,exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
    env['SDCC_HOME']=str(support);env['PATH']=str(support/'bin')+os.pathsep+env['PATH']
    rows=[]
    for abi in (2,4):
      for model in ('small','large'):
       for opt in ('size','speed'):
        d=root/f'abi{abi}-{model}-{opt}';generate(d)
        for name in ('input','wide','legacy','cases'):
            flags=['-mmcs251','--std-sdcc11','--stack-auto','--mcs251-native-abi','--mcs251-machine-ir',
                   '--mcs251-function-metadata','--model-'+model,'--opt-code-'+opt,f'-DTEST_ABI={abi}']
            if name!='legacy':flags+=['--mcs251-wide-ordinary']
            user_stress.run([sdcc,*flags,'-c',name+'.c'],d,env,name)
            asm=(d/(name+'.asm')).read_text()
            refusals=re.findall(r'; wide (?:refusal|admission) function=(\S+).*',asm)
            if name!='legacy': assert not refusals,(d,name,refusals)
            rows.append(dict(abi=abi,model=model,opt=opt,unit=name,
                conversions=asm.count('; wide boolean conversion'),refusals=refusals))
    board.write(root/'results.json',dict(compilerSha256=board.sha(sdcc),rows=rows))
    print('PASS: 32 units, ABI2/4, small/large, size/speed',flush=True)


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
