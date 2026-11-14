"""Check fixed EDATA dynamic indexing and unchanged unknown/volatile paths."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('edata-indexed-addresses.c').read_bytes()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain);env['PATH']=str(a.toolchain/'bin')+os.pathsep+env['PATH']
def code(text):
 return '\n'.join(l for raw in text.splitlines() if (l:=raw.split(';')[0].strip()) and not re.match(r'ar[0-7]\s*=',l))
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
    if name.startswith('negative_') or name.startswith('positive_volatile_'):
     assert code(old[name])==code(body),(root,name,'address boundary changed')
    elif name.startswith('positive_'):
     assert 'wide edata indexed ' in body,(root,name)
     assert 'instruction-fallback' not in body,(root,name)
     selected.append(name)
   for name in ('positive_byte','positive_word','positive_dword','positive_loop'):
    assert 'wide edata indexed store' in new[name] and 'wide edata indexed load' in new[name],(root,name)
   for name in ('shift_masked','shift_byte'):
    assert 'wide byte shift count ' in new[name],(root,name)
   for version in ('before','after'):
    samples=[l.strip() for l in versions[version]['positive_sampled_index'].splitlines() if re.search(r'^\s*mov\s+.*e:\(_index_sample\+',l)]
    assert len(samples)==3,(root,version,samples)
   rows.append(dict(model=model,opt=opt,abi=abi,selected=selected,unchangedAddressBoundaries=8,volatileIndexSamples=3))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: 8 configurations; byte/word/dword indexing, shifts, 8 unchanged address boundaries and index samples')
