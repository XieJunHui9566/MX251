"""Check native runtime selection and DATA snapshots across ABI4 calls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','support','output'):
    p.add_argument('--'+key,required=True,type=Path)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).parent
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.support.resolve())
env['PATH']=str(a.support.resolve()/'bin')+';'+env['PATH']
rows=[]

def body(text,name):
    return text.split('; mx251-function begin schema=1 symbol=_'+name+' ',1)[1].split('; mx251-function end',1)[0]

def compile(cc,d,model,opt,source,output):
    command=[str(cc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
             '--stack-auto','--mcs251-wide-ordinary','--mcs251-native-abi',
             '--mcs251-native-int-runtime','--mcs251-machine-ir','--mcs251-function-metadata',
             '-c',source+'.c','-o',output+'.rel']
    q=subprocess.run(command,cwd=d,env=env,capture_output=True,timeout=60)
    (d/(output+'.log')).write_bytes(q.stdout+q.stderr)
    assert q.returncode==0,(command,q.stderr)
    rows.append(dict(command=command,output=str(d),exitCode=q.returncode))
    return (d/(output+'.asm')).read_text()

for model in ('small','large'):
    for opt in ('speed','size'):
        d=a.output/(model+'-'+opt);d.mkdir()
        for name in ('native-call-composition.h','native-call-composition.c','native-call-callees.c'):
            shutil.copy2(tests/name,d/name)
        (d/'volatile.c').write_text('''#include "native-call-composition.h"
volatile __data u32 device_value;
volatile __data u16 device_word;
u32 volatile_register(u32 b) { return native_mix(device_value,b,3); }
u32 volatile_stack(u32 a,u32 b) { return native_mix(a,b,device_word); }
''')
        parent=compile(a.baseline,d,model,opt,'native-call-composition','parent')
        new=compile(a.sdcc,d,model,opt,'native-call-composition','candidate')
        for name in ('constant_reload','object_reload','snapshot_calls','byte_snapshot'):
            assert 'wide-mir accepted: '+name in body(new,name),name
        assert 'mx251-narrow-negate function=_constant_reload selected=wide' in body(new,'constant_reload')
        assert 'mx251-narrow-negate function=_object_reload selected=wide' in body(new,'object_reload')
        for name in ('object_reload','snapshot_calls','byte_snapshot'):
            assert 'wide native argument snapshots' in body(new,name),name
            assert 'argument-snapshot-abi4-contract' in body(parent,name),name
        assert 'wide-mir accepted: constant_reload' not in body(parent,'constant_reload')
        compile(a.sdcc,d,model,opt,'native-call-callees','callees')
        volatile=compile(a.sdcc,d,model,opt,'volatile','volatile')
        for name in ('volatile_register','volatile_stack'):
            assert 'argument-snapshot-abi4-contract' in body(volatile,name),name

(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=4,
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),commands=rows),indent=2)+'\n')
print('PASS: small/large x speed/size; native division selection, byte/word/dword snapshots, volatile ABI4 path retained')
