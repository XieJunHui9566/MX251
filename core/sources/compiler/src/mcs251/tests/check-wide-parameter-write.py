"""Parameter storage width, snapshot ordering, and unchanged fallback boundaries."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False);T=Path(__file__).resolve().parent;rows=[]
prefix='typedef unsigned long u32;typedef unsigned char u8;\n#define ABI __reentrant __sdcccall(4)\n'
negative={
 'address-third':'u32 g(u32 __xdata *) ABI;u32 f(u32 a,u32 b,u32 c) ABI {c=g(&c);return c+a+b;}',
 'address-eighth':'u32 g(u32 __xdata *) ABI;u32 f(u32 a,u32 b,u32 c,u32 d,u32 e,u32 h,u32 i,u32 j) ABI {j=g(&j);return a+b+c+d+e+h+i+j;}',
 'volatile':'u32 f(u32 a,u32 b,volatile u32 c) ABI {c=a+b;return c;}',
 'volatile-pointer':'u32 f(u32 a,u32 b,u8 __xdata * volatile c) ABI {c=(u8 __xdata *)a;return (u32)c+b;}',
 'nine':'u32 f(u32 a,u32 b,u32 c,u32 d,u32 e,u32 h,u32 i,u32 j,u32 k) ABI {c=a+b;return c+d+e+h+i+j+k;}',
}
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir()
  for name in ['wide-parameter-write.h','wide-parameter-write-callees.c']:(d/name).write_bytes((T/name).read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','wide-parameter-write-callees.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
  asm=(d/'wide-parameter-write-callees.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert len(accepted)==11 and 'wide-mir fallback:' not in asm
  stores=[tuple(map(int,q)) for q in re.findall(r'wide stack parameter store width=(\d+) entry-offset=(-?\d+)',asm)]
  assert set(w for w,o in stores)=={1,2,3,4}
  assert all(pair in stores for pair in [(1,-3),(2,-5),(3,-6),(4,-7),(1,-10),(2,-12),(4,-16)])
  assert not re.search(r'\br(?:1[6-9]|[23]\d)\b',asm),'Unencodable byte register'
  frame=asm.split('wide-mir accepted: p_frame;',1)[1].split('wide-mir accepted:',1)[0]
  assert 'frame=17' in frame and 'wide stack parameter store width=4 entry-offset=-7' in frame
  assert 'reg=-1' in asm.split('wide-mir accepted: p_pressure;',1)[1]
  rows.append(dict(model=model,opt=opt,accepted=accepted,stores=stores,localFrame=17,spills=True))
 for name,src in negative.items():
  objects=[]
  for enabled in [False,True]:
   d=a.output_dir/(model+'-'+name+'-'+str(enabled));d.mkdir();(d/'test.c').write_text(prefix+src)
   args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi']+(['--mcs251-wide-backend'] if enabled else [])+['-c','test.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,(name,q.stderr)
   asm=(d/'test.asm').read_text()
   if name.startswith('address-') and enabled:
    assert 'wide-mir accepted: f;' in asm and 'wide parameter address entry-offset=' in asm and 'wide stack parameter store width=4' in asm
   else:assert 'wide-mir accepted:' not in asm,name
   objects.append((d/'test.rel').read_bytes())
  if name.startswith('address-'):rows.append(dict(model=model,boundary=name,addressedStackFormalAccepted=True))
  else:
   assert objects[0]==objects[1],name
   rows.append(dict(model=model,boundary=name,fallbackObjectIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'parameter-write configurations/boundaries')
