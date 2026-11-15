"""Check shared loop analysis through both backends and calling conventions."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support.resolve()),PATH=str(a.support.resolve()/'bin')+os.pathsep+env['PATH'])
rows=[]
for model in ('small','large'):
    for backend in ('legacy','wide'):
        for abi in (2,4):
            d=a.output/f'{model}-{backend}-abi{abi}';d.mkdir()
            shutil.copy2(tests/'loop-facts.c',d/'probe.c')
            command=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto',
                     '--opt-code-speed','--std-sdcc11','--mcs251-machine-ir',
                     '--mcs251-native-int-runtime','--mcs251-function-metadata',
                     '-DTEST_ABI='+str(abi),'--dump-i-code','-c','probe.c']
            if backend=='wide':command+=['--mcs251-wide-ordinary']
            q=subprocess.run(command,cwd=d,env=env,capture_output=True,timeout=120)
            (d/'compile.log').write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,q.stderr
            ir=(d/'probe.dumpgenconstprop_1').read_text()
            asm=(d/'probe.asm').read_text()
            funcs={m[1]:m[2] for m in re.finditer(r'\tproc _(?:__m251r4_)?(loop_\w+)(.*?)\teproc _(?:__m251r4_)?\1\b',ir,re.S)}
            assert len(funcs)==20,funcs.keys()
            assert not re.search(r'call \S*impossible\b',funcs['loop_small'])
            # Negative remainders and externally changed counters must retain
            # their signed semantics; the hardware fixture checks the values.
            for name in ('loop_negative','loop_down','loop_redefine','loop_escape','loop_dynamic','loop_entries'):
                assert ' % ' in funcs[name],name
            sections={m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
                r'; mx251-function end schema=1',asm,re.S)}
            for name in ('loop_small','loop_large'):
                body=next(v for k,v in sections.items() if k.endswith('_'+name))
                assert not re.search(r'\b(?:ecall|lcall|call)\s+\S*mod\w*',body),(d,name)
            assert (d/'probe.rel').is_file()
            rows.append(dict(model=model,backend=backend,abi=abi,status='PASS'))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2)+'\n')
print('PASS: 8 configurations, shared dead branch folding and signed loop coverage')
