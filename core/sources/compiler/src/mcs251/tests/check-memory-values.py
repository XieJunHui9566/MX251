"""Compile independent memory-version, alias and byte-view workloads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','support','output'):
    p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support.resolve()),MX251_WIDE_VALUE_LIMIT='1024',
           MX251_WIDE_NODE_LIMIT='2048',MX251_BACKEND_CANDIDATES='1')
env['PATH']=str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
rows=[]
for model in ('small','large'):
    for abi in (2,4):
        variants={}
        for name,compiler in [('before',a.baseline),('after',a.sdcc)]:
            root=a.output/f'{model}-abi{abi}'/name;root.mkdir(parents=True)
            for file in ('memory-values.c','memory-values.h'):
                shutil.copy2(Path(__file__).parent/file,root/file)
            cmd=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto',
                 '--opt-code-speed','--mcs251-wide-ordinary','--mcs251-machine-ir',
                 '--mcs251-function-metadata','-DTEST_ABI='+str(abi),'-c','memory-values.c']
            q=subprocess.run(cmd,cwd=root,env=env,capture_output=True,timeout=180)
            (root/'compiler.log').write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,(root,q.stderr)
            asm=(root/'memory-values.asm').read_text(encoding='latin1')
            size=sum(int(x,16) for x in re.findall(r'^A CSEG(?:\$\S+)? size ([0-9A-F]+)',
                          (root/'memory-values.rel').read_text(),re.M))
            variants[name]=dict(bytes=size,asm=asm)
        assert variants['after']['bytes']<=variants['before']['bytes'],(model,abi)
        asm=variants['after']['asm']
        assert re.search(r'memory-values choice function=mv_constant .*selected=1',asm)
        assert re.search(r'memory-values choice function=mv_forward .*selected=1',asm)
        rows.append(dict(model=model,abi=abi,before=variants['before']['bytes'],after=variants['after']['bytes']))
result=dict(status='PASS',rows=rows,hardwareExecuted=False,
            compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
            baselineSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest())
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',newline='\n')
print(json.dumps(result))
