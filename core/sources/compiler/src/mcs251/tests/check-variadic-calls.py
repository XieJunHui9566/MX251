"""Compile all-stack ABI2 calls, promotions and existing fallback boundaries."""
import argparse,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('sdcc','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent;rows=[]
env=os.environ.copy();env['SDCC_HOME']=str(a.support.resolve());env['PATH']=str(a.support.resolve()/'bin')+';'+env['PATH']
extra='''#include "variadic-call.h"
u16 promotions(signed char a,u8 b) { return variadic_packed(7,a,b); }
u16 cast_bytes(u8 a) { return variadic_packed(7,(u8)a); }
u32 native_caller(u32 a) __sdcccall(4) { return variadic_samples(7,a,a)+a; }
void variadic_void(u16 tag,...);
void void_caller(u16 a) { variadic_void(a,0x12345678UL); }
u16 too_many(u16 a) { return variadic_many(16,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a); }
u16 wide_extra(u16 a) { return variadic_many(1,0x1122334455667788ULL)+a; }
'''
def function(text,name):
    return text.split('; mx251-function begin schema=1 symbol=_'+name+' ',1)[1].split('; mx251-function end',1)[0]
def compile(d,source,model,opt,standard='sdcc11',wide=True):
    cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--std-'+standard,'--stack-auto',
         '--mcs251-native-abi','--mcs251-function-metadata','--mcs251-machine-ir']
    if wide:cmd+=['--mcs251-wide-ordinary']
    cmd+=['-c',source+'.c']
    q=subprocess.run(cmd,cwd=d,capture_output=True,env=env)
    (d/(source+'.log')).write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
    rows.append(dict(command=cmd,output=str(d),exitCode=q.returncode))
    return (d/(source+'.asm')).read_text()
for model in ('small','large'):
    for opt in ('speed','size'):
        d=a.output/(model+'-'+opt);d.mkdir()
        for name in ('machine-board.h','variadic-call.h','variadic-callers.c','variadic-callees.c'):shutil.copy2(tests/name,d/name)
        (d/'extra.c').write_text(extra)
        text=compile(d,'variadic-callers',model,opt)
        assert 'wide-mir accepted: variadic_case' in text
        assert 'function=_variadic_case selected=wide' in text
        assert 'arguments=16 stack-bytes=31 registers=0' in text
        assert 'wide argument snapshot' in text
        assert 'wide symbolic stack argument' in text
        assert '; MX251 argument region begin' in text
        assert 'wide ordinary result forwarded physical=4' in function(text,'variadic_float_caller')
        compile(d,'variadic-callees',model,opt,wide=False)
        text=compile(d,'extra',model,opt)
        for name in ('promotions','cast_bytes','native_caller','void_caller'):
            assert 'wide-mir accepted: '+name in text
        assert 'arguments=3 stack-bytes=5 registers=0' in function(text,'promotions')
        assert 'arguments=2 stack-bytes=2 registers=0' in function(text,'cast_bytes')
        for name in ('too_many','wide_extra'):
            assert 'wide-mir accepted: '+name not in text
        # Strict C promotes the explicitly cast byte as well; SDCC mode above
        # deliberately retains its existing one-byte extension.
        text=compile(d,'extra',model,opt,standard='c11')
        assert 'arguments=2 stack-bytes=3 registers=0' in function(text,'cast_bytes')
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=4,commands=rows),indent=2)+'\n')
print('PASS: small/large x speed/size, 16 all-stack args, ABI4 caller, C/SDCC promotions, snapshots and return transport')
