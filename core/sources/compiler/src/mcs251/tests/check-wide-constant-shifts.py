#!/usr/bin/env python3
"""Compile the constant-shift/pressure corpus; board oracles live separately."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser()
p.add_argument('--sdcc',type=Path,required=True)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();(d/'shifts.c').write_bytes(a.source.read_bytes())
 args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-wide-backend','-c','shifts.c']
 r=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(r.stdout+r.stderr);assert not r.returncode,r.stderr
 asm=(d/'shifts.asm').read_text();accepted=dict(re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm))
 assert len(accepted)==150 and 'wide-mir fallback' not in asm
 assert all(int(accepted[n])>0 for n in ['pressure_l32','pressure_r32','pressure_s32','pressure_l16','pressure_r16','pressure_s16'])
 assert 'reg=24' in asm and 'reg=28' in asm
 assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',asm), 'Upper word registers have no byte aliases'
 for name in ['l32_8','r32_8','s32_8','l32_16','r32_16','s32_16','l32_24','r32_24','s32_24']:
  body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
  assert not re.search(r'\b(?:srl|sra)\b|\bdec\s+a\b',body),name
 rows.append(dict(model=model,args=args,accepted=accepted,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2))
print('PASS: 150 accepted shift functions per model; real upper registers/spills; byte/word fixed shifts have no bit loop')
