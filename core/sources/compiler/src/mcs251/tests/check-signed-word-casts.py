#!/usr/bin/env python3
"""Keep explicit signed-int conversion after unsigned GETWORD formation."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for arch in ['mcs251','mcs51']:
 for model in ['small','large']:
  d=a.output_dir/(arch+'-'+model);d.mkdir();src=a.source.read_text()
  (d/'probe.c').write_text(src if arch=='mcs251' else src.replace(' __sdcccall(4)',''))
  args=[str(a.sdcc.resolve()),'-m'+arch,'--model-'+model,'--stack-auto','--opt-code-speed','--dump-i-code',*(['--mcs251-native-abi'] if arch=='mcs251' else []),'-c','probe.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  ir=(d/'probe.dumpgcse').read_text();casts=len(re.findall(r'\{int fixed\} = \(int fixed\)',ir))
  assert casts==3,(arch,model,casts)
  rows.append(dict(arch=arch,model=model,args=args,signedCastsPreserved=casts,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS four shared frontend configurations; board execution is checked separately')
