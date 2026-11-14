"""Check equal-arm selection, call sites, and non-equivalent branch boundaries."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('equal-branches.c').read_bytes()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain);env['PATH']=str(a.toolchain/'bin')+';'+env['PATH']
rows=[]
def code(text):
 return '\n'.join(l for l in text.splitlines() if l and not l.startswith(';') and not re.match(r'ar[0-7]\s*=',l.strip()))
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
    versions[name]={m[1].lstrip('_').removeprefix('m251r4_'):m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
                    r'; mx251-function end schema=1',text,re.S)}
   before,after=versions['before'],versions['after']
   selected=[n for n,t in after.items() if re.search(r'^; wide equal-branch choice .*selected=1 ',t,re.M)]
   assert {'equal_indirect','equal_chain','equal_nested'}<=set(selected),(base,selected)
   boundaries=['different_target','different_argument','different_order','different_reads',
               'different_pointer','different_result','external_entry','switch_entry','different_literals']
   for n in boundaries:assert code(before[n])==code(after[n]),(base,n)
   calls=lambda t:len(re.findall(r'^\s*ecall\s+@dr',t,re.M))
   assert calls(before['equal_indirect'])==2 and calls(after['equal_indirect'])==1,base
   # A volatile condition remains a physical read even when its IF disappears.
   reads=lambda t:len(re.findall(r'^\s*mov\s+[^,]+,\(?_flag',t,re.M))
   if model=='small':assert reads(before['equal_condition'])==reads(after['equal_condition'])>0,base
   rows.append(dict(model=model,opt=opt,abi=abi,selected=selected,unchanged=boundaries,
                    indirectSitesBefore=2,indirectSitesAfter=1))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: eight configurations; identical calls merged, nine different/control-entry cases unchanged')
