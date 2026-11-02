#!/usr/bin/env python3
"""Check extraction locations, overlap safety, spill offsets, and volatile read width."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();commands=[]
 def compile(name,src):
  (d/(name+'.c')).write_text(src)
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-native-abi','--mcs251-wide-backend','-c',name+'.c']
  r=subprocess.run(args,cwd=d,capture_output=True);(d/(name+'.log')).write_bytes(r.stdout+r.stderr);commands.append(dict(args=args,exitCode=r.returncode));assert not r.returncode,r.stderr
  return (d/(name+'.asm')).read_text()
 asm=compile('core',a.source.read_text());accepted=dict(re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm))
 assert len(accepted)==24 and 'wide-mir fallback' not in asm
 assert all(int(v)>0 for k,v in accepted.items() if k.startswith('pressure_'))
 paths={k:asm.count('wide getword path='+k) for k in ['word','bytes','upper','spill']};assert all(paths.values())
 slots=[tuple(map(int,x)) for x in re.findall(r'path=spill slot=(\d+) index=(\d+)',asm)]
 assert {i for s,i in slots}=={0,1,2} and {(s+i)%2 for s,i in slots}=={0,1}
 assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',asm)
 for name in ['extract_u0','extract_u8','extract_u16','s0','s8','s16']:
  body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
  assert 'mov\tdr16' not in body and not re.search(r'\b(?:srl|sra)\b',body),name
 fallback=compile('volatile_fallback','unsigned int f(volatile unsigned long __xdata *p) __reentrant __sdcccall(4) {return (unsigned int)(*p>>8);}')
 assert 'wide-mir fallback: f' in fallback and len(re.findall(r'mov\s+a,@dpx\b',fallback))==4
 volatile=compile('volatile','unsigned int f(unsigned long a,unsigned long b) __reentrant __sdcccall(4) {volatile unsigned long __xdata *p=(volatile unsigned long __xdata *)a;unsigned long x=*p;return (unsigned int)(x>>8);}')
 assert 'wide-mir accepted: f;' in volatile and 'wide memory load width=4 volatile-bytes=1' in volatile
 assert len(re.findall(r'mov\s+a,@dr16\b',volatile))==4
 assert not re.search(r'mov\s+wr\d+,@dr16\b',volatile)
 rows.append(dict(model=model,accepted=accepted,paths=paths,spillSlices=slots,volatileBytesRead=4,commands=commands,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS: 24 accepted functions/model, four extraction locations, actual spills at both byte parities, four ordered volatile byte reads')
