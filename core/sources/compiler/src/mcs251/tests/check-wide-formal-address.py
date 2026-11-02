"""Register homes, original stack parameter addresses, alias lifetime and fallback."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False);T=Path(__file__).resolve().parent;rows=[]
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir()
  for n in ['wide-formal-address.h','wide-formal-address-callers.c']:(d/n).write_bytes((T/n).read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','wide-formal-address-callers.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(d/'wide-formal-address-callers.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert len(accepted)==14 and re.findall(r'wide-mir fallback: (\w+)',asm)==['c_float']
  bodies={n:asm.split('wide-mir accepted: '+n+';',1)[1].split('; mcs251 wide-mir',1)[0] for n in accepted}
  for n,s in [('c_first','a'),('c_second','b')]:
   body=bodies[n];assert f'wide object {s} slot=0 size=4' in body and 'frame=4 ' in body
   assert body.index('wide local snapshot store')<body.index('\tecall\t')<body.index('wide local snapshot load')
  third=bodies['formal_third'];assert 'frame=0 ' in third and 'wide object ' not in third
  assert 'wide parameter address entry-offset=-6' in third and 'wide stack parameter store width=4 entry-offset=-6' in third
  mixed=bodies['formal_mixed'];assert re.findall(r'wide object (\w+) slot=\d+ size=(\d+)',mixed)==[('a','1'),('b','2')]
  assert set(map(int,re.findall(r'wide parameter address entry-offset=(-?\d+)',mixed)))=={-3,-5,-17}
  assert 'wide stack parameter width=3 result=4 entry-offset=-8' in mixed
  assert 'reg=-1' in bodies['formal_pressure']
  checked=[]
  for name,body in bodies.items():
   frame=int(re.search(r'frame=(\d+)',body)[1]);objects=[tuple(map(int,x)) for x in re.findall(r'wide object \w+ slot=(\d+) size=(\d+)',body)]
   if objects or 'wide parameter address' in body:assert not re.search(r'\tejmp\t_(?!_wm_)',body)
   occupied=set()
   for s,z in objects:
    region=set(range(s,s+z));assert occupied.isdisjoint(region) and s+z<=frame;occupied|=region
   values=[tuple(map(int,x[:4]))+(int(x[4].replace(':',''),16),) for x in re.findall(r'; wide v(\d+) \S+ width=(\d+) reg=(-?\d+) slot=(-?\d+) edges=([0-9a-f:]+)',body)]
   for i,z,g,s,e in values:
    if g>=0:continue
    assert s>=0 and s+z<=frame and occupied.isdisjoint(range(s,s+z))
    for j,w,h,t,edges in values:
     if j<i and h<0 and e&(1<<j):assert set(range(s,s+z)).isdisjoint(range(t,t+w))
   checked.append(dict(function=name,frame=frame,objects=objects,lifetimeAndSpills=True))
  rows.append(dict(model=model,opt=opt,accepted=accepted,checks=checked,physicalPointerBytes=3,parameterOffsets=[-3,-5,-8,-12,-13,-17]))
negative={
 'register-pointer':'void g(u32 __xdata * __xdata *) ABI;u32 f(u32 __xdata *p,u32 b) ABI {g(&p);return (u32)p+b;}',
 'stack-pointer':'void g(u32 __xdata * __xdata *) ABI;u32 f(u32 a,u32 b,u32 __xdata *p) ABI {g(&p);return (u32)p+a+b;}',
 'volatile-register':'void g(volatile u32 __xdata *) ABI;u32 f(volatile u32 a,u32 b) ABI {g(&a);return a+b;}',
 'volatile-stack':'void g(volatile u32 __xdata *) ABI;u32 f(u32 a,u32 b,volatile u32 c) ABI {g(&c);return a+b+c;}',
}
header='typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
for model in ['small','large']:
 for name,src in negative.items():
  objects=[]
  for wide in [False,True]:
   d=a.output_dir/(model+'-'+name+'-'+str(wide));d.mkdir();(d/'test.c').write_text(header+src)
   q=subprocess.run([str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi',*(['--mcs251-wide-backend'] if wide else []),'-c','test.c'],cwd=d,capture_output=True)
   (d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
   asm=(d/'test.asm').read_text()
   if wide and name in ['register-pointer','stack-pointer']:
    assert 'wide-mir accepted: f;' in asm
    if name=='register-pointer':assert 'wide object p slot=0 size=3' in asm and 'wide pointer object store physical=3 logical=4' in asm
    else:assert 'wide parameter address entry-offset=-5' in asm and 'wide stack parameter width=3 result=4 entry-offset=-5' in asm
   else:assert 'wide-mir accepted:' not in asm,name
   objects.append((d/'test.rel').read_bytes())
  if name in ['register-pointer','stack-pointer']:rows.append(dict(model=model,boundary=name,pointerObjectAccepted=True))
  else:
   assert objects[0]==objects[1],name
   rows.append(dict(model=model,boundary=name,fallbackIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'formal address configurations/boundaries')
