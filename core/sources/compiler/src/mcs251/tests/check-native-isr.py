"""ISR entry/exit, whole-body selection, frame/call coverage and opt-out."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','parent','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
repo=Path(__file__).resolve().parents[6]
sys.path.insert(0,str(repo/'tools/research'))
import native_isr_fixture
fixture=a.output/'fixture';native_isr_fixture.create(fixture)
source=(fixture/'call-boundary-isr.c').read_text()
header=(fixture/'tail-boundary.h').read_text()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support),MX251_WIDE_VALUE_LIMIT='1024',MX251_WIDE_NODE_LIMIT='2048')
env['PATH']=str(a.support/'bin')+os.pathsep+env['PATH']
rows=[]


def compile_at(root,compiler,text,options=(),disabled=False):
    root.mkdir(parents=True);(root/'test.c').write_text(text);(root/'tail-boundary.h').write_text(header)
    cmd=[str(compiler),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto',
         '--mcs251-wide-ordinary','--mcs251-function-metadata','--mcs251-machine-ir',
         '--mcs251-flat-code-reads',*options,'-c','test.c']
    q=subprocess.run(cmd,cwd=root,env=dict(env,**({'MX251_NATIVE_ISR':'0'} if disabled else {})),capture_output=True)
    (root/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,(root,q.stderr)
    return (root/'test.asm').read_text(),(root/'test.rel').read_bytes()


for model in ('small','large'):
    for opt in ('speed','size'):
        root=a.output/(model+'-'+opt)
        asm,_=compile_at(root/'new',a.sdcc,source)
        for name,frame in [('context_irq',0),('context_leaf_irq',9)]:
            body=asm.split('; mx251-function begin schema=1 symbol=_'+name+' ',1)[1].split('; mx251-function end',1)[0]
            assert f'mx251-isr-choice function=_{name} selected=wide' in body,(root,name)
            assert f'mx251-native-isr frame={frame}' in body,(root,name)
            ops=[s for line in body.splitlines() if (s:=line.split(';',1)[0].strip())]
            assert sum(x=='reti' for x in ops)==1 and 'eret' not in ops,(root,name)
            assert re.search(r'push\s+0xd1\b',body) and re.search(r'pop\s+0xd1\b',body)
            assert not re.search(r'ejmp\s+_context_(?:sum|clobber)\b',body)
        _,off=compile_at(root/'disabled',a.sdcc,source,disabled=True)
        _,parent=compile_at(root/'parent',a.parent,source)
        assert off==parent,(root,'disabled object changed')
        negatives={
            'bank':' __using(1)',
            'critical':' __critical',
            'naked':' __naked',
        }
        for name,attr in negatives.items():
            body='__asm reti __endasm;' if name=='naked' else 'total+=0x12345678UL;'
            text='volatile __edata unsigned long total; void irq(void) __interrupt(1)'+attr+'{'+body+'}'
            new,obj=compile_at(root/name/'new',a.sdcc,text)
            _,old=compile_at(root/name/'parent',a.parent,text)
            assert 'mx251-native-isr' not in new and obj==old,(root,name)
        rows.append(dict(model=model,optimization=opt,nativeFunctions=2,localFrameBytes=9,
                         explicitOptOutIdentical=True,excludedEntries=list(negatives)))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),rows=rows),indent=2)+'\n')
print('PASS: 4 configurations; 8 native ISR bodies, frames/calls, opt-out and 12 excluded entries')
