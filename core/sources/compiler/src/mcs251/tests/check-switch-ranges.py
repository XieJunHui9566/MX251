"""Assemble bounded switch intervals under both models, policies and ABI modes."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdcc',type=Path,required=True)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
rows=[]
for model in ('small','large'):
 for policy in ('speed','size'):
  for abi in ('ordinary','native'):
   for enabled in (False,True):
    out=a.output/f'{model}-{policy}-{abi}-{int(enabled)}';out.mkdir()
    source=a.source.read_text()
    if abi=='native':source=re.sub(r'(u16\s+\w+\([^)]*\))\s*\{',r'\1 __reentrant __sdcccall(4) {',source)
    (out/'ranges.c').write_text(source)
    env=dict(os.environ,MX251_SWITCH_RANGES='1' if enabled else '0')
    cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+policy,
         '--mcs251-wide-ordinary' if abi=='ordinary' else '--mcs251-wide-backend',
         '-I'+str(a.source.parent.resolve()),'-c','ranges.c','-o','ranges.rel']
    if abi=='native':cmd.insert(1,'--mcs251-native-abi')
    run=subprocess.run(cmd,cwd=out,env=env,capture_output=True)
    (out/'compile.log').write_bytes(run.stdout+run.stderr)
    assert run.returncode==0,run.stderr
    asm=(out/'ranges.asm').read_text()
    accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
    assert {'range_small','range_signed','range_full','range_tail'}<=set(accepted),accepted
    intervals=re.findall(r'wide switch ranges entries=(\d+) runs=(\d+) table-min-bytes=(\d+) range-max-bytes=(\d+)',asm)
    tables=re.findall(r'wide jump table entries=(\d+)',asm)
    if enabled:
     assert sorted((int(e),int(r))for e,r,_,_ in intervals)==[(5,3),(5,3),(256,3)],intervals
     assert not tables,tables
     assert all(int(new)<int(old)for _,_,old,new in intervals)
     assert re.search(r'\bj(?:le|g)\b',asm)
    else:
     assert not intervals
     assert sorted(map(int,tables))==[5,5,256],tables
    rows.append(dict(model=model,policy=policy,abi=abi,enabled=enabled,intervals=intervals,tables=tables,status='PASS'))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2)+'\n')
print('PASS:',len(rows),'switch interval configurations')
