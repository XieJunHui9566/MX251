"""Independent callback values, tables and cross-backend ABI execution tests."""
import argparse
import json
import os
from pathlib import Path

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress

CASES = ['local', 'local_array', 'data_scalar', 'edata_scalar', 'xdata_scalar',
         'data_array', 'edata_array', 'xdata_array', 'code_array', 'struct_member',
         'wide_first', 'wide_second', 'wide_third', 'wide_return',
         'legacy_first', 'legacy_second', 'legacy_third', 'legacy_return', 'legacy_invoke',
         'stack_arguments', 'pressure', 'repeated']


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    header = '''#ifndef CALLBACK_FIXTURE_H
#define CALLBACK_FIXTURE_H
#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define __data
#define __edata
#define __xdata
#define __code
#define ABI
#endif
typedef u32 (*Operation)(u32, u8) ABI;
u32 add(u32 x,u8 y) ABI;
u32 mix(u32 x,u8 y) ABI;
typedef u32 (*Many)(u32,u8,u16,u32,const u8 *,const u8 *) ABI;
u32 many(u32 x,u8 y,u16 a,u32 b,const u8 *p,const u8 *q) ABI;
u32 pressure(Operation fn,u32 x,u8 y,u32 a,u32 b,u32 c,u32 d,u32 e) ABI;
u32 stress_run(u8 kind,u8 seed);
'''
    for prefix in ('wide', 'legacy'):
        header += f'''u32 {prefix}_first(Operation fn,u32 x,u8 y) ABI;
u32 {prefix}_second(u32 x,Operation fn,u8 y) ABI;
u32 {prefix}_third(u32 x,u8 y,Operation fn) ABI;
Operation {prefix}_choose(u8 y) ABI;
'''
    header += 'u32 legacy_invoke(u32 x,u8 y) ABI;\n#endif\n'
    (root/'stress.h').write_text(header)
    (root/'targets.c').write_text('''#include "stress.h"
u32 add(u32 x,u8 y) ABI {return x+(u32)y*257UL+0x1234UL;}
u32 mix(u32 x,u8 y) ABI {return (x<<3)^(x>>7)^y;}
u32 many(u32 x,u8 y,u16 a,u32 b,const u8 *p,const u8 *q) ABI {
    return (x+b)^((u32)a<<8)^y^((u16)*p<<4)^*q;
}
''')
    for prefix in ('wide', 'legacy'):
        source = '#include "stress.h"\n'
        for position, args in [('first','Operation fn,u32 x,u8 y'),
                               ('second','u32 x,Operation fn,u8 y'),
                               ('third','u32 x,u8 y,Operation fn')]:
            source += f'u32 {prefix}_{position}({args}) ABI {{return fn(x,y);}}\n'
        source += f'Operation {prefix}_choose(u8 y) ABI {{return (y&1)?add:mix;}}\n'
        if prefix == 'wide':
            source += '''u32 pressure(Operation fn,u32 x,u8 y,u32 a,u32 b,u32 c,u32 d,u32 e) ABI {
    u32 r=fn(x,y); r=fn(r,(u8)(y+1));
    return r^a^(b<<1)^(c>>1)^(d+e);
}
'''
        if prefix == 'legacy':
            source += 'u32 legacy_invoke(u32 x,u8 y) ABI { return wide_third(x,y,(y&1)?add:mix); }\n'
        (root/(prefix+'.c')).write_text(source)
    source = '''#include "stress.h"
static Operation volatile __data dfn;
static Operation volatile __edata efn;
static Operation volatile __xdata xfn;
static Operation __data dt[2];
static Operation volatile __edata et[2];
static Operation __xdata xt[2];
static Operation const __code ct[2]={mix,add};
struct Record {u8 before; Operation fn; u8 after;};
static struct Record __xdata records[2];
static Many volatile many_fn;
static u8 anchor=0x37;
'''
    for kind, name in enumerate(CASES):
        setup = ''; call = 'fn(x,y)'
        if name == 'local': setup = 'fn=(y&1)?add:mix;'
        elif name == 'stack_arguments':
            setup = 'u8 local=(u8)(y^0xa5); many_fn=many;'
            call = 'many_fn(x,y,0x9abcU,0x12345678UL,&local,&anchor)'
        elif name == 'pressure':
            setup = 'fn=(y&1)?add:mix;'
            call = 'pressure(fn,x,y,x+1,x^0xfedcba98UL,x+3,x+4,x+5)'
        elif name == 'repeated':
            setup = 'fn=(y&1)?add:mix; x=fn(x,y); x=fn(x,(u8)(y+1));'
        elif name == 'local_array': setup = 'Operation t[2]; t[0]=mix; t[1]=add; fn=*(t+(y&1));'
        elif name.endswith('_scalar'):
            obj = {'data_scalar':'dfn','edata_scalar':'efn','xdata_scalar':'xfn'}[name]
            setup = f'{obj}=(y&1)?add:mix; fn={obj};'
        elif name.endswith('_array'):
            obj = {'data_array':'dt','edata_array':'et','xdata_array':'xt','code_array':'ct'}[name]
            if obj != 'ct': setup = f'{obj}[0]=mix; {obj}[1]=add;'
            setup += f'fn={obj}[y&1];'
        elif name == 'struct_member':
            setup = 'struct Record *p=&records[y&1]; p->before=0x5a; p->after=0xa5; p->fn=(y&1)?add:mix; fn=p->fn;'
            call = '(fn(x,y) ^ (u32)(p->before^0x5a) ^ (u32)(p->after^0xa5))'
        elif name.endswith('_return'): call = f'{name.split("_")[0]}_choose(y)(x,y)'
        elif name == 'legacy_invoke': call = 'legacy_invoke(x,y)'
        else:
            setup = 'fn=(y&1)?add:mix;'
            args = {'first':'fn,x,y','second':'x,fn,y','third':'x,y,fn'}[name.split('_')[1]]
            call = f'{name}({args})'
        source += f'''u32 test_{name}(u8 seed) {{
    u8 i,y; u32 x=0xabcdef01UL^seed; Operation fn;
    for(i=0;i<12;++i) {{ y=seed+i; {{ {setup} x={call}; }} }}
    return x;
}}
'''
    source += 'u32 stress_run(u8 kind,u8 seed) {switch(kind) {\n'
    source += ''.join(f'case {k}: return test_{n}(seed);\n' for k,n in enumerate(CASES))
    source += 'default:return 0;} }\n'
    (root/'cases.c').write_text(source)
    board.write(root/'source.json',dict(cases=CASES,files={p.name:board.sha(p)
        for p in sorted(root.iterdir()) if p.suffix in ('.c','.h')}))


def reference(root,gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\n'
        f'int main(void){{unsigned k,s; for(k=0;k<{len(CASES)};++k) for(s=0;s<64;++s) '
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall',*sorted(p.name for p in root.glob('*.c')),
                     '-o','reference.exe'],root,label='compile')
    values={k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'],root).splitlines())}
    for k in range(CASES.index('stack_arguments')):
        for s in range(64): assert values[f'{k}:{s}']==values[f'0:{s}'],(k,s)
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
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(callbacks NONE)\n'
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
        for name in ('targets','wide','legacy','cases'):
            flags=['-mmcs251','--stack-auto','--mcs251-native-abi','--mcs251-machine-ir',
                   '--mcs251-function-metadata','--model-'+model,'--opt-code-'+opt,f'-DTEST_ABI={abi}']
            if name!='legacy':flags+=['--mcs251-wide-ordinary']
            user_stress.run([sdcc,*flags,'-c',name+'.c'],d,env,name)
            asm=(d/(name+'.asm')).read_text()
            import re
            refusals=re.findall(r'; wide (?:refusal|admission) function=(\S+).*',asm)
            if name!='legacy': assert not refusals,(d,name,refusals)
            if name in ('wide','cases'):
                assert '; wide indirect call abi='+str(abi) in asm,(d,name,'missing indirect call')
            rows.append(dict(abi=abi,model=model,opt=opt,unit=name,
                indirectCalls=asm.count('; wide indirect call'),refusals=refusals))
    board.write(root/'results.json',rows)
    print('PASS: 32 units, ABI2/4, small/large, size/speed',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='command',required=True)
    q=s.add_parser('reference');q.add_argument('--gcc',type=Path,required=True)
    q.add_argument('--output',type=Path,required=True)
    q=s.add_parser('board');q.add_argument('--abi',type=int,choices=(2,4),required=True)
    q.add_argument('--output',type=Path,required=True)
    q=s.add_parser('contracts')
    for n in ('output','sdcc','support'):q.add_argument('--'+n,type=Path,required=True)
    q=s.add_parser('capture')
    for n in ('output','hex','reference'):q.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    if a.command=='reference':reference(a.output,a.gcc)
    elif a.command=='board':create_board(a.output,a.abi)
    elif a.command=='contracts':contracts(a.output,a.sdcc,a.support)
    else:bitfield_fixture.capture(a.hex,a.output,a.reference)
