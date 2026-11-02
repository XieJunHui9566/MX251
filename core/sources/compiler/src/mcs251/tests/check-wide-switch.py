#!/usr/bin/env python3
"""Compile native switch tables, including all 256 selectors and live spills.

Execution is separately qualified with independent reference vectors on the MCU.
This check does not claim runtime validation.
"""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sdcc',required=True);p.add_argument('--source',required=True);p.add_argument('--output-dir',required=True);a=p.parse_args()
d=Path(a.output_dir).resolve();d.mkdir(exist_ok=False);rows=[]
names=['dense8','gapped8','signed8','nonzero16','wide32','full256','nested_switch','pressure_switch','volatile_switch','calls_switch']
for model in ['small','large']:
 for opt in ['speed','size']:
  out=d/(model+'-'+opt);out.mkdir();(out/'test.c').write_text(Path(a.source).read_text(),encoding='utf8')
  cmd=[str(Path(a.sdcc).resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-abi','--mcs251-wide-backend','-c','test.c']
  q=subprocess.run(cmd,cwd=out,capture_output=True);(out/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(out/'test.asm').read_text();assert re.findall(r'wide-mir accepted: (\w+)',asm)==names
  tables=list(map(int,re.findall(r'wide jump table entries=(\d+)',asm)));assert tables==[8,11,9,12,12,256,8,8,8,8,8],tables
  frame=int(re.search(r'accepted: pressure_switch;[^\n]*frame=(\d+)',asm)[1]);assert frame>0
  assert asm.count('ejmp\t@dr16')==len(tables)
  rows.append(dict(model=model,opt=opt,tables=tables,pressureFrame=frame,assembled=True,hardwareExecuted=False))
(d/'results.json').write_text(json.dumps(rows,indent=2));print('PASS switch tables,4 configurations')
