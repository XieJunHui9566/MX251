"""Addressed scalar alias storage, spill lifetime and volatile transactions."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False);T=Path(__file__).resolve().parent;rows=[]
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir()
  for n in ['wide-local-scalar.h','wide-local-scalar-callers.c']:(d/n).write_bytes((T/n).read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','wide-local-scalar-callers.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(d/'wide-local-scalar-callers.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert len(accepted)==11 and re.findall(r'wide-mir fallback: (\w+)',asm)==['c_float']
  checks=[]
  for name in accepted:
   body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
   frame=int(re.search(r'frame=(\d+)',body)[1]);objects=re.findall(r'; wide object (\w+) slot=(\d+) size=(\d+)',body)
   if name=='c_recurse':assert not objects;continue
   assert len(objects)==len(set(n for n,s,z in objects))
   expected_count=2 if name=='c_alias' else 3 if name=='c_frame' else 1
   assert len(objects)==expected_count,(name,objects)
   occupied=set()
   for n,s,z in objects:
    s=int(s);z=int(z);region=set(range(s,s+z));assert occupied.isdisjoint(region) and s+z<=frame;occupied|=region
   values=[tuple(map(int,x[:4]))+(int(x[4].replace(':',''),16),) for x in re.findall(r'; wide v(\d+) \S+ width=(\d+) reg=(-?\d+) slot=(-?\d+) edges=([0-9a-f:]+)',body)]
   for i,z,g,s,e in values:
    if g>=0:continue
    assert s>=0 and s+z<=frame and occupied.isdisjoint(range(s,s+z))
    for j,w,h,t,edges in values:
     if j<i and h<0 and e&(1<<j):assert set(range(s,s+z)).isdisjoint(range(t,t+w)),(name,i,j)
   if name=='c_pressure':assert any(g<0 for i,z,g,s,e in values)
   snapshots=re.findall(r'; wide local snapshot (load|store) (\S+)\n; wide memory (load|store) width=([124]) volatile-bytes=([01])\n; wide stack direct object=(\d+) offset=(\d+)',body)
   assert snapshots and all(op==memop and int(obj)<len(objects) and off=='0' and int(width)==int(objects[int(obj)][2]) for op,sym,memop,width,vol,obj,off in snapshots)
   assert not re.search(r'\tejmp\t_(?!_wm_)',body),'addressed frame must outlive every call'
   if name=='c_volatile':
    assert [op for op,sym,memop,width,vol,obj,off in snapshots]==['store','load','load','store','load','load']
    assert all(width=='4' and vol=='1' for op,sym,memop,width,vol,obj,off in snapshots)
    parts=body.split('; wide local snapshot ')[1:]
    for part in parts:
     op=part.split()[0]
     pattern=r'\tmov\t'+(r'a,@spx([+-]0x[0-9a-f]+)?' if op=='load' else r'@spx([+-]0x[0-9a-f]+)?,a')
     offsets=[int(v,16) if v else 0 for v in re.findall(pattern,part)]
     assert offsets==list(range(-3,1)),(op,offsets)
   checks.append(dict(function=name,objects=objects,frame=frame,snapshots=len(snapshots),disjoint=True))
  rows.append(dict(model=model,opt=opt,accepted=accepted,checks=checks,volatileTransactions=6,volatileBytes=24))
negative={
 'addressed-pointer':'void g(u32 __xdata * __xdata *) ABI;u32 f(u32 a) ABI {u32 __xdata *p=(u32 __xdata *)a;g(&p);return (u32)p;}',
 'addressed-bool':'void g(_Bool __xdata *) ABI;u32 f(u32 a) ABI {_Bool b=a!=0;g(&b);return b;}',
 'addressed-formal':'void g(u32 __xdata *) ABI;u32 f(u32 a) ABI {g(&a);return a;}',
}
prefix='typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
for model in ['small','large']:
 for name,src in negative.items():
  objects=[]
  for enabled in [False,True]:
   d=a.output_dir/(model+'-'+name+'-'+str(enabled));d.mkdir();(d/'test.c').write_text(prefix+src)
   args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi']+(['--mcs251-wide-backend'] if enabled else [])+['-c','test.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
   asm=(d/'test.asm').read_text()
   if name=='addressed-formal' and enabled:
    assert 'wide-mir accepted: f;' in asm and 'wide object a slot=0 size=4' in asm and 'wide local snapshot load' in asm
   elif name=='addressed-pointer' and enabled:
    assert 'wide-mir accepted: f;' in asm and 'wide object p slot=0 size=3' in asm
    assert 'wide pointer object store physical=3 logical=4' in asm and 'wide pointer object load physical=3 logical=4' in asm
   else:assert 'wide-mir accepted:' not in asm,name
   objects.append((d/'test.rel').read_bytes())
  if name in ['addressed-formal','addressed-pointer']:rows.append(dict(model=model,boundary=name,addressedObjectAccepted=True))
  else:
   assert objects[0]==objects[1],name
   rows.append(dict(model=model,boundary=name,fallbackObjectIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'local scalar configurations/boundaries')
