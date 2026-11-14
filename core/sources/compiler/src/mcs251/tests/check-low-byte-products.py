"""Check native byte products, full products, spills and volatile sampling."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('low-byte-products.c').read_bytes()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain);env['PATH']=str(a.toolchain/'bin')+os.pathsep+env['PATH']
def code(text):
 return '\n'.join(l for l in text.splitlines() if l and not l.startswith(';') and not re.match(r'ar[0-7]\s*=',l.strip()))
def transactions(body):
 return [l.strip() for l in body.splitlines() if re.search(r'^\s*mov\s+.*e:\(_samples\+',l)]
rows=[]
for model in ('small','large'):
 for opt in ('size','speed'):
  for abi in (2,4):
   root=a.output/f'{model}-{opt}-{abi}';versions={}
   for name,exe in [('before',a.baseline),('after',a.sdcc)]:
    d=root/name;d.mkdir(parents=True);(d/'probe.c').write_bytes(source)
    cmd=[str(exe),'-mmcs251','--stack-auto','--mcs251-native-abi','--mcs251-wide-ordinary',
         '--mcs251-machine-ir','--mcs251-function-metadata','--model-'+model,
         '--opt-code-'+opt,'--std-sdcc11',f'-DTEST_ABI={abi}','-c','probe.c']
    run=subprocess.run(cmd,cwd=d,env=env,capture_output=True)
    (d/'compile.log').write_bytes(run.stdout+run.stderr);assert not run.returncode,(d,run.stderr)
    asm=(d/'probe.asm').read_text();assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',asm)
    versions[name]={m[1].lstrip('_').removeprefix('m251r4_'):m[2] for m in re.finditer(
     r'; mx251-function begin schema=1 symbol=(\S+)(.*?); mx251-function end schema=1',asm,re.S)}
   old,new=versions['before'],versions['after'];selected=[]
   for name,body in new.items():
    if 'wide low-byte multiply' not in body:continue
    assert 'instruction-fallback' not in body,(root,name)
    selected.append(name)
    assert re.search(r'^\s*mul\s+r11,r(?:[0-9]|1[0-5])$',body,re.M),(root,name)
   assert set(selected)>={'low_unsigned','low_signed','low_mixed','low_constant','low_live','low_pressure','low_sampled'},root
   assert re.search(r'wide-mir accepted: low_pressure;.*frame=[1-9]',new['low_pressure']),root
   assert 'rhs=10' in new['low_pressure'],root
   assert transactions(old['low_sampled'])==transactions(new['low_sampled']) and transactions(new['low_sampled']),root
   assert code(old['full_product'])==code(new['full_product']),root
   rows.append(dict(model=model,opt=opt,abi=abi,selected=selected,volatileOrderUnchanged=True,fullProductUnchanged=True))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: 8 configurations; byte products, unchanged full product and volatile transactions')
