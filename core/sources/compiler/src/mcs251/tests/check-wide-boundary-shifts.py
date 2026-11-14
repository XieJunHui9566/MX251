"""Compare boundary shifts, high-register pressure and volatile transactions."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('wide-boundary-shifts.c').read_bytes()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain);env['PATH']=str(a.toolchain/'bin')+os.pathsep+env['PATH']
def code(text):
 return '\n'.join(l for l in text.splitlines() if l and not l.startswith(';') and not re.match(r'ar[0-7]\s*=',l.strip()))
def transactions(body):
 return [l.strip() for l in body.splitlines() if re.search(r'^\s*mov\s+.*e:\(_sample\+',l)]
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
   for name in ['logical_'+str(k) for k in (7,15,23)]+['signed_'+str(k) for k in (7,15,23)]:
    assert 'wide boundary right shift' in new[name],(root,name)
    assert not re.search(r'^\s*(?:srl|sra)\s+|^\s*dec\s+a',new[name],re.M),(root,name)
    selected.append(name)
   for name in ('pressure_logical','pressure_signed'):
    assert re.search(r'wide-mir accepted: '+name+r';.*frame=[1-9]',new[name]),(root,name)
    assert re.search(r'wide boundary right shift .*work=(?:16|24|28)',new[name]),(root,name)
   assert 'wide boundary right shift count=31 signed=1' in new['pressure_signed'],root
   for name in selected+['pressure_logical','pressure_signed','sampled_logical','sampled_signed']:
    assert 'instruction-fallback' not in new[name],(root,name)
   for name in ('sampled_logical','sampled_signed'):
    assert transactions(old[name])==transactions(new[name]) and transactions(new[name]),(root,name)
   for name in ('logical_31','unchanged_other','unchanged_variable'):
    assert code(old[name])==code(new[name]),(root,name)
   rows.append(dict(model=model,opt=opt,abi=abi,selected=selected,pressure=True,volatileOrderUnchanged=True))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: 8 configurations; boundary shifts, upper registers/spills, unchanged volatile order and other shifts')
