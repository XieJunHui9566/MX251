"""Check native quotient/remainder pairing with actual assembler output."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','toolchain','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain.resolve())
env['PATH']=str(a.toolchain.resolve()/'bin')+os.pathsep+env['PATH']
rows=[]
positive=('pair_qr','pair_rq','pair_byte','pair_byte_wide','pair_truncated','pair_loop')
negative=('pair_changed','pair_branch','pair_volatile')
for model in ('small','large'):
    for opt in ('speed','size'):
        for abi in ('ordinary','native'):
            d=a.output/(model+'-'+opt+'-'+abi);d.mkdir()
            for name in ('native-divmod-pairs.c','native-divmod-pairs.h'):
                shutil.copy2(Path(__file__).with_name(name),d/name)
            cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                 '--stack-auto','--mcs251-machine-ir','--mcs251-function-metadata',
                 '--mcs251-wide-ordinary' if abi=='ordinary' else '--mcs251-wide-backend']
            if abi=='native':cmd+=['--mcs251-native-abi','-DPAIR_ABI=__reentrant __sdcccall(4)']
            sizes={}
            for mode in ('enabled','disabled'):
                runenv=env if mode=='enabled' else dict(env,MX251_NATIVE_DIVMOD='0')
                q=subprocess.run(cmd+['-c','native-divmod-pairs.c','-o',mode+'.rel'],cwd=d,env=runenv,capture_output=True)
                (d/(mode+'.log')).write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
                asm=(d/(mode+'.asm')).read_text()
                sizes[mode]=int(re.search(r'^A CSEG size (\w+)',(d/(mode+'.rel')).read_text(),re.M)[1],16)
                if mode=='enabled':
                    for name in positive:
                        assert re.search(r'^; wide native-divmod choice function='+name+r' pairs=1 selected=1\b',asm,re.M),(d,name)
                        body=asm.split('_'+name+':\n',1)[1].split('; mx251-function end',1)[0]
                        assert len(re.findall(r'^\s*div\s+',body,re.M))==1,(d,name)
                    for name in negative:
                        assert not re.search(r'wide native-divmod choice function='+name+r'\b',asm),(d,name)
                else:assert 'division result=pair' not in asm
            assert sizes['enabled']<sizes['disabled'],(d,sizes)
            rows.append(dict(model=model,optimization=opt,abi=abi,bytes=sizes))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2)+'\n')
print('PASS: 8 configurations; quotient/remainder order, byte/word/truncated results, loops, changed values, branches and volatile samples')
