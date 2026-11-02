#!/usr/bin/env python3
"""Check actual low-byte products, retained operands, calls and real spills."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
names=['low8','widened','retained8','constant8','chain8','branch8','calls8','pressure8']
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','test.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(d/'test.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert accepted==names and 'wide-mir fallback:' not in asm
  assert asm.count('wide unsigned multiply 8x8 -> 8')==9
  assert int(re.search(r'wide-mir accepted: pressure8;.*frame=(\d+)',asm)[1])>0
  assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',asm)
  rows.append(dict(model=model,optimize=opt,args=args,accepted=accepted,realSpills=True,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS8 functions x4 configurations, low-byte multiply and real spills')
