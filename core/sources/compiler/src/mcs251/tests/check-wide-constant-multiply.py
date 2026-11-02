"""离线检查调用身份和展开边界；运算正确性由独立主机 oracle/实板记录验证。"""
from pathlib import Path
import argparse,subprocess,json
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes())
 cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-wide-backend','--mcs251-native-int-runtime','-c','test.c']
 r=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(r.stdout+r.stderr);assert not r.returncode
 asm=(d/'test.asm').read_text()
 def body(n):return asm.split('wide-mir accepted: '+n+';',1)[1].split('; mcs251 wide-mir',1)[0]
 for n in ['mul13','mul7','mul_minus3','mul_signed','mul_live']:
  assert 'wide constant multiply:' in body(n) and 'ecall' not in body(n),n
 for n in ['mul_dense','mul_dynamic','user_call']:
  assert 'wide constant multiply:' not in body(n),n
 assert '___m251r4_user_mullong' in body('user_call')
 assert '___m251r4__mullong' in body('mul_dense')
 rows.append(dict(model=model,args=cmd,passed=True,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS constant lowering and runtime/user-call boundaries in two models')
