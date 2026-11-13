"""Check byte assembly choices, original byte transactions, and the off switch."""
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
rows=[]
def instructions(text):return [s.strip() for s in text.splitlines() if s.startswith('\t') and not s.strip().startswith('.')]
def body(text,name):return text.split('symbol=_'+name+' ',1)[1].split('; mx251-function end',1)[0]
for model in ('small','large'):
    for opt in ('speed','size'):
        assemblies={}
        for variant in ('parent','off','new'):
            d=a.output/(model+'-'+opt)/variant;d.mkdir(parents=True)
            for file in ('byte-pack.c','byte-pack.h'):shutil.copy2(tests/file,d/file)
            compiler=a.baseline if variant=='parent' else a.sdcc
            command=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                '--stack-auto','--mcs251-native-abi','--mcs251-wide-ordinary','--mcs251-machine-ir',
                '--mcs251-function-metadata','-c','byte-pack.c']
            e=dict(env,MX251_BYTE_PACK='0' if variant=='off' else '1')
            q=subprocess.run(command,cwd=d,env=e,capture_output=True)
            (d/'compile.log').write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,(command,q.stderr)
            assemblies[variant]=(d/'byte-pack.asm').read_text()
        assert instructions(assemblies['parent'])==instructions(assemblies['off'])
        selected=re.findall(r'^; wide byte-pack choice function=(\S+) .*?selected=1\b',assemblies['new'],re.M)
        for name in ('pack_be','pack_le','pack_word','pack_or','pack_xor','pack_volatile','pack_repeat'):
            assert name in selected,(name,selected)
        for name in ('pack_signed','pack_overlap','pack_shared'):
            assert name not in selected,(name,selected)
        volatile=body(assemblies['new'],'pack_volatile')
        assert volatile.count('wide single-byte load direct-reg=')==4
        assert len(re.findall(r'\bmov\s+(?:a|r\d+),@dr\d+',volatile))==4
        assert not re.search(r'\br(?:1[6-9]|[23]\d)\b',assemblies['new'])
        rows.append(dict(model=model,optimization=opt,selected=selected,offMatchesParent=True,volatileByteReads=4))
record=dict(status='PASS',compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),configurations=rows)
(a.output/'result.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record))
