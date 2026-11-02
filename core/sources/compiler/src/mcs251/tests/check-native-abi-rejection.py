"""Unsupported ABI definitions/direct/indirect calls must fail before lowering."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args();a.output_dir.mkdir(exist_ok=False)
cases={
 'definition-bit-arg':'u32 f(__bit a) ABI {return a;}',
 'definition-bit-result':'__bit f(u32 a) ABI {return a!=0;}',
 'direct-bit-arg':'u32 h(__bit a) ABI;u32 f(u32 a){return h(a);}',
 'direct-bit-result':'__bit h(u32 a) ABI;u32 f(u32 a){return h(a);}',
 'indirect-bit-arg':'u32 (*p)(__bit) ABI;u32 f(u32 a){return p(a);}',
 'indirect-bit-result':'__bit (*p)(u32) ABI;u32 f(u32 a){return p(a);}',
 'definition-longlong':'u32 f(unsigned long long a) ABI {return (u32)a;}',
 'direct-longlong':'u32 h(unsigned long long a) ABI;u32 f(u32 a){return h(a);}',
 'native-caller-bad-callee':'u32 h(__bit a) ABI;u32 f(u32 a) ABI {return h(a);}',
}
rows=[]
for model in ['small','large']:
 for wide in [False,True]:
  for abi in [3,4]:
   for name,body in cases.items():
    d=a.output_dir/('%s-%s-%d-%s'%(model,'wide' if wide else 'legacy',abi,name));d.mkdir()
    (d/'bad.c').write_text('typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(%d)\n'%abi+body)
    args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi']+(['--mcs251-wide-backend'] if wide else [])+['-c','bad.c']
    q=subprocess.run(args,cwd=d,capture_output=True,timeout=15);log=q.stdout+q.stderr;(d/'compile.log').write_bytes(log)
    assert q.returncode==1 and b'ABI' in log,(name,q.returncode,log)
    assert not re.search(rb'Internal Error|SIGSEGV|Segmentation|Assertion',log),(name,log)
    assert not (d/'bad.rel').exists(),name
    rows.append(dict(case=name,abi=abi,model=model,wide=wide,exitCode=q.returncode,cleanRejection=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'clean ABI rejections')
