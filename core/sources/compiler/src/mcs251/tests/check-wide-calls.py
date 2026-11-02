#!/usr/bin/env python3
"""R31 compile checks. Hardware/oracle results are retained separately by the board harness."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser()
p.add_argument('--sdcc',type=Path,required=True)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();(d/'calls.c').write_bytes(a.source.read_bytes())
 cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-wide-backend','--mcs251-native-int-runtime','-c','calls.c']
 r=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(r.stdout+r.stderr);assert not r.returncode
 asm=(d/'calls.asm').read_text();accepted=dict(re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm));fallback=re.findall(r'wide-mir fallback: (\w+)',asm)
 assert len(accepted)==33 and fallback==['sign16call'],(accepted,fallback)
 def body(name):return asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
 for name in ['tail32','swap32','swap16','swap8','sign16return']:
  assert '\tejmp\t___m251r4_' in body(name) and '\tecall\t' not in body(name),name
 assert '\tpush\tdr24' in asm and '\tpush\tdr28' in asm
 for name in ['spillargs','bytespillargs']:
  text=body(name);depth=0;reads=[];at_call=[]
  for line in text.splitlines():
   m=re.match(r'\s*(push|pop)\s+(dr|wr|r)\d+',line)
   if m:depth+=(1 if m[1]=='push' else -1)*{'dr':4,'wr':2,'r':1}[m[2]]
   if re.match(r'\s*mov\s+(wr\d+|a),@spx',line) and depth:reads.append(line)
   if '\tecall\t' in line:at_call+=reads;reads=[]
  assert depth==0 and at_call,(name,depth,at_call)
 assert '\teret' in body('voidtail')
 rows.append(dict(model=model,command=cmd,accepted=accepted,fallback=fallback,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2))
print('PASS: 33 accepted functions and one BitInt24 fallback in both stack-auto models')
