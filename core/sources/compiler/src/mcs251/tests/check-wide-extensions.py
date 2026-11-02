#!/usr/bin/env python3
"""Check unsigned extension fusion and overlapping narrow ABI inputs."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();(d/'core.c').write_text(a.source.read_text())
 args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-native-abi','--mcs251-wide-backend','-c','core.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
 asm=(d/'core.asm').read_text();accepted={n:int(c) for n,c in re.findall(r'wide-mir accepted: (\w+).*folded-casts=(\d+)',asm)}
 assert len(accepted)==29 and 'wide-mir fallback' not in asm
 for width in [8,16]:
  for op in ['add','sub','xor','or','and']:
   for side in ['left','right']:assert accepted[f'{side}{width}_{op}']==1
  body=asm.split(f'wide-mir accepted: right{width}_sub;',1)[1].split('; mcs251 wide-mir',1)[0]
  save='movz\twr22,r7' if width==8 else 'mov\twr22,wr6'
  assert body.index(save)<body.index('mov\tdr4,dr12')<body.index('sub\tdr4,dr20')
 for name in ['signed_'+x for x in ['add','sub','xor','or','and']]+['multiuse','identity']:assert not accepted[name]
 rows.append(dict(model=model,args=args,exitCode=q.returncode,folds=accepted,overlapSnapshotVerified=True,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 29 functions/model; unsigned extensions and both narrow ABI overlap snapshots; signed/multiuse exclusions')
