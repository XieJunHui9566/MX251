"""Compare narrow shifts, zero-high bitwise operations and sampled accesses."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('narrow-zero-shifts.c').read_bytes()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain);env['PATH']=str(a.toolchain/'bin')+os.pathsep+env['PATH']
def code(text):
 return '\n'.join(l for l in text.splitlines() if l and not l.startswith(';') and not re.match(r'ar[0-7]\s*=',l.strip()))
def transactions(body):
 return [re.sub(r'\br(?:[0-9]|1[0-5])\b','Rn',l.strip()) for l in body.splitlines() if re.search(r'^\s*mov\s+.*e:\(_sample\+',l)]
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
   old,new=versions['before'],versions['after']
   shifted=[name for name in ('byte_8','byte_16','byte_24','byte_word','word_8','word_16','word_24','pressure') if 'wide narrow left shift' in new[name]]
   assert len(shifted)>=6,(root,shifted)
   bitwise=[name for name in ('xor_byte','xor_word','or_word','and_word','pressure') if 'wide zero-high bitwise' in new[name]]
   assert len(bitwise)>=4,(root,bitwise)
   for name in ('signed_xor','signed_left','full_left','reused_cast'):
    assert code(old[name])==code(new[name]),(root,name)
   assert transactions(old['sampled'])==transactions(new['sampled']) and len(transactions(new['sampled']))==5,root
   assert re.search(r'wide-mir accepted: pressure;.*frame=[1-9]',new['pressure']),root
   for name in shifted+bitwise+['variable_byte','variable_word','sampled']:
    assert 'instruction-fallback' not in new[name],(root,name)
   rows.append(dict(model=model,opt=opt,abi=abi,shifted=shifted,zeroHighBitwise=bitwise,pressureSpills=True,volatileTransactions=5,unchangedBoundaries=4))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: 8 configurations, narrow shifts/bitwise, spills, 5 ordered volatile transactions, signed and reused boundaries')
