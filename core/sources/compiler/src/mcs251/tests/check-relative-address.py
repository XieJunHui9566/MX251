"""Check relative address generation and preserve ordinary memory transactions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env['SDCC_HOME']=str(a.support.resolve());env['PATH']=str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
instructions=lambda text:[s.strip() for s in text.splitlines() if s.startswith('\t') and not s.strip().startswith('.')]
body=lambda text,name:text.split('symbol=_'+name+' ',1)[1].split('; mx251-function end',1)[0]
rows=[]
for model in ('small','large'):
    for opt in ('speed','size'):
        assemblies={}
        for variant in ('parent','off','new'):
            d=a.output/(model+'-'+opt)/variant;d.mkdir(parents=True)
            for file in ('relative-address.c','relative-address.h'):shutil.copy2(tests/file,d/file)
            compiler=a.baseline if variant=='parent' else a.sdcc
            command=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                '--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata',
                '-c','relative-address.c']
            q=subprocess.run(command,cwd=d,env=dict(env,MX251_RELATIVE_ADDRESS='0' if variant=='off' else '1'),capture_output=True)
            (d/'compile.log').write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,q.stderr
            assemblies[variant]=(d/'relative-address.asm').read_text()
        assert instructions(assemblies['parent'])==instructions(assemblies['off'])
        counts={}
        for name in ('forward','reverse','negative','volatile','calls','branch','scratch','changed','store'):
            before=body(assemblies['parent'],'address_'+name);after=body(assemblies['new'],'address_'+name)
            counts[name]=after.count('; wide relative-address ')
            transactions=lambda t:re.findall(r'^\s*(mov(?:c|x)?)\s+([^;\n]*@[^;\n]*)',t,re.M)
            assert transactions(before)==transactions(after),name
        for name in ('forward','reverse','negative'):assert counts[name]>=2,(name,counts)
        # In large model the global source uses DR16 for its own XDATA load.
        if model=='small':assert counts['store']>=2,counts
        assert counts['volatile']==0,counts
        assert counts['calls']==counts['branch']==0,counts
        assert 'delta=-1' in body(assemblies['new'],'address_reverse')
        assert not re.search(r'\br(?:1[6-9]|[23]\d)\b',assemblies['new'])
        rows.append(dict(model=model,optimization=opt,reused=counts,offMatchesParent=True,memoryTransactionsIdentical=True))
record=dict(status='PASS',compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),configurations=rows)
(a.output/'result.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record))
