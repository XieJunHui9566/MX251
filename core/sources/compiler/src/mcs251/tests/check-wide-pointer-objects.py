"""Physical three-byte pointer storage, disjoint frames, transaction order, boundaries."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False);T=Path(__file__).resolve().parent;rows=[]
def compile_one(name,model,src=None,wide=True,opt='speed'):
 d=a.output_dir/name;d.mkdir()
 for n in ['wide-pointer-objects.h','wide-pointer-objects-callers.c']:(d/n).write_bytes((T/n).read_bytes())
 if src is not None:(d/'test.c').write_text('#include "wide-pointer-objects.h"\n'+src)
 fn='test' if src is not None else 'wide-pointer-objects-callers'
 args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi','--opt-code-'+opt,*(['--mcs251-wide-backend'] if wide else []),'-c',fn+'.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(name,q.stderr)
 return (d/(fn+'.asm')).read_text(),(d/(fn+'.rel')).read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  asm,obj=compile_one(model+'-'+opt,model,opt=opt)
  _,obj2=compile_one(model+'-'+opt+'-repeat',model,opt=opt);assert obj==obj2
  accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==14 and 'wide-mir fallback:' not in asm
  assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
  bodies={n:asm.split('wide-mir accepted: '+n+';',1)[1].split('; mcs251 wide-mir',1)[0] for n in accepted}
  for n in ['first_pointer','second_pointer']:
   body=bodies[n];assert 'frame=3 ' in body and re.search(r'wide object [pq] slot=0 size=3',body)
   assert body.index('wide pointer object store')<body.index('\tecall\t')<body.index('wide pointer object load')
  assert 'wide parameter address entry-offset=-5' in bodies['third_pointer'] and 'frame=0 ' in bodies['third_pointer']
  assert set(map(int,re.findall(r'entry-offset=(-?\d+)',bodies['mixed_pointer'])))=={-5,-6,-8,-11,-12,-16}
  assert 'wide object g slot=0 size=5' in bodies['c_bits']
  # 压力函数在封存父版已可无 spill；不能把低效分配当成正确性要求。
  # 下方仍检查每个实际 spill 与对象边界、相交活区间的槽位隔离。
  assert re.search(r'wide v\d+ .*width=4 reg=',bodies['c_pressure'])
  checked=[]
  for n,body in bodies.items():
   frame=int(re.search(r'frame=(\d+)',body)[1]);occupied=set()
   objects=re.findall(r'wide object (\w+) slot=(\d+) size=(\d+)',body)
   if objects or 'wide parameter address' in body:assert not re.search(r'\tejmp\t_(?!_wm_)',body)
   for sym,slot,size in objects:
    slot=int(slot);size=int(size);region=set(range(slot,slot+size))
    assert slot>=0 and slot+size<=frame and occupied.isdisjoint(region);occupied|=region
    if sym in ['p','q']:assert size==3
   values=[tuple(map(int,x[:4]))+(int(x[4].replace(':',''),16),) for x in re.findall(r'; wide v(\d+) \S+ width=(\d+) reg=(-?\d+) slot=(-?\d+) edges=([0-9a-f:]+)',body)]
   for i,z,g,s,e in values:
    if g>=0:continue
    assert s>=0 and s+z<=frame and occupied.isdisjoint(range(s,s+z))
    for j,w,h,t,edges in values:
     if j<i and h<0 and e&(1<<j):assert set(range(s,s+z)).isdisjoint(range(t,t+w))
   checked.append(dict(function=n,frame=frame,objects=objects,disjoint=True))
  volatile=bodies['c_volatile'];parts=re.findall(r'; wide pointer object (load|store) physical=3 logical=4 volatile-bytes=1\n(.*?)(?=\n;|\Z)',volatile,re.S)
  assert [op for op,b in parts]==['store','load','store','load','load','load']
  for op,body in parts:
   pat=r'\tmov\t'+(r'a,@spx([+-]0x[0-9a-f]+)?' if op=='load' else r'@spx([+-]0x[0-9a-f]+)?,a')
   assert [int(x,16) if x else 0 for x in re.findall(pat,body)]==[-2,-1,0]
  rows.append(dict(model=model,opt=opt,accepted=accepted,checks=checked,volatileTransactions=6,volatileBytes=18,deterministic=True))
positive={
 'read':'P32 f(P32 PTR *p) ABI {return *p;}',
 'write':'void f(P32 PTR *p,P32 q) ABI {*p=q;}',
 'volatile-read':'P32 f(P32 volatile PTR *p) ABI {return *p;}',
 'volatile-write':'void f(P32 volatile PTR *p,P32 q) ABI {*p=q;}',
 'generic':'u32 *f(u32 **p) ABI {return *p;}',
}
negative={
 'code-value':'u32 f(u32 a) ABI {return (u32)*(u8 __code * __xdata *)a;}',
 'code-object':'u32 f(u32 a) ABI {return (u32)*(u8 __xdata * __code *)a;}',
 'near-value':'u32 f(u32 a) ABI {return (u32)*(u8 __idata * __xdata *)a;}',
 'function-value':'typedef void (*F)(void);u32 f(u32 a) ABI {return (u32)*(F __xdata *)a;}',
}
for model in ['small','large']:
 for n,src in positive.items():
  asm,_=compile_one(model+'-'+n,model,src);assert 'wide-mir accepted: f;' in asm
  part=asm.split('; wide pointer object ',1)[1];op='store' if n.endswith('write') else 'load'
  assert part.startswith(op+' physical=3 logical=4 volatile-bytes='+str(int(n.startswith('volatile'))))
  if n.startswith('volatile'):
   pat=r'\tmov\t'+(r'a,@dr16(?:\+(\d+))?' if op=='load' else r'@dr16(?:\+(\d+))?,a')
   assert [int(x) if x else 0 for x in re.findall(pat,part)]==[0,1,2]
  else:assert ('mov\twr22,@dr16+1' if op=='load' else 'mov\t@dr16+1,wr22') in part
  rows.append(dict(model=model,positive=n,physicalBytes=3))
 for n,src in negative.items():
  asm,obj=compile_one(model+'-'+n,model,src);_,old=compile_one(model+'-'+n+'-legacy',model,src,False)
  assert 'wide-mir accepted:' not in asm and obj==old,n
  rows.append(dict(model=model,boundary=n,fallbackIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'pointer object configurations/boundaries')
