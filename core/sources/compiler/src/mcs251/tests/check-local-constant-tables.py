"""Compile local table propagation and compare mutable/escaping boundaries."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('local-constant-tables.c').read_bytes()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain);env['PATH']=str(a.toolchain/'bin')+';'+env['PATH']
rows=[]
for model in ('small','large'):
 for opt in ('size','speed'):
  for abi in (2,4):
   base=a.output/f'{model}-{opt}-{abi}';versions={}
   for name,compiler in [('before',a.baseline),('after',a.sdcc)]:
    d=base/name;d.mkdir(parents=True);(d/'probe.c').write_bytes(source)
    args=['-mmcs251','--stack-auto','--mcs251-native-abi','--mcs251-wide-ordinary',
          '--mcs251-machine-ir','--mcs251-function-metadata','--model-'+model,
          '--opt-code-'+opt,'--std-sdcc11',f'-DTEST_ABI={abi}','-c','probe.c']
    run=subprocess.run([str(compiler),*args],cwd=d,env=env,capture_output=True)
    (d/'compile.log').write_bytes(run.stdout+run.stderr);assert not run.returncode,(d,run.stderr)
    text=(d/'probe.asm').read_text()
    versions[name]={m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
                    r'; mx251-function end schema=1',text,re.S)}
   get=lambda version,name:next(v for k,v in versions[version].items() if k.endswith('_'+name))
   selected=[]
   for name in ('bytes','words','dwords','data_pointers','callbacks','volatile_index','eight_words','eight_callbacks','pointer_literals','mixed_objects'):
    text=get('after',name)
    if not (name=='volatile_index' and model=='large'):
     assert 'wide-mir accepted: '+name in text,(base,name)
    if '; wide constant lookup ' in text:selected.append(name)
   assert {'callbacks','bytes','dwords','eight_words','eight_callbacks','pointer_literals','mixed_objects'}<=set(selected),(base,selected)
   for name in ('code_pointers','volatile_table','mutable_table','escaped_table','partial_table','conditional_table','unbounded_table','wrapped_table'):
    code=lambda text:'\n'.join(l for l in text.splitlines() if l and not l.startswith(';') and not re.match(r'ar[0-7]\s*=',l.strip()))
    assert code(get('before',name))==code(get('after',name)),(base,name)
   text=get('after','volatile_index')
   old=get('before','volatile_index')
   reads=lambda s:len(re.findall(r'^\s*mov\s+[^,]+,\(?_sampled_index',s,re.M))
   if model=='small':assert reads(text)==reads(old)>0,(base,'volatile sampling')
   else:assert code(text)==code(old),(base,'volatile large fallback')
   rows.append(dict(model=model,opt=opt,abi=abi,selected=selected,boundaryFunctionsUnchanged=8))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: eight configurations, constant tables and eight unchanged semantic boundaries')
