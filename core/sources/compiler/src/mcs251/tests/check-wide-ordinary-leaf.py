"""ABI2 entry/return, physical stack layout, opt-in isolation and leaf boundaries."""
from pathlib import Path
import argparse,subprocess,re,json
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def compile_one(name,model,source,flags,compiler=None,stack=True,opt='speed'):
 d=a.output_dir/name;d.mkdir()
 for corpus in ['ordinary','pointer-objects','ordinary-leaf']:(d/('wide-'+corpus+'.h')).write_bytes((T/('wide-'+corpus+'.h')).read_bytes())
 (d/'test.c').write_text(source)
 cmd=[str((compiler or a.sdcc).resolve()),'-mmcs251','--model-'+model,*(['--stack-auto'] if stack else []),'--opt-code-'+opt,*flags,'-c','test.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  for corpus,count in [('ordinary',9),('pointer-objects',3),('ordinary-leaf',10)]:
   name=model+'-'+opt+'-'+corpus;source=(T/('wide-'+corpus+'-callees.c')).read_text()
   asm,obj=compile_one(name,model,source,['--mcs251-wide-ordinary'],opt=opt)
   _,again=compile_one(name+'-repeat',model,source,['--mcs251-wide-ordinary'],opt=opt);assert obj==again
   accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==count
   assert asm.count('wide ordinary ABI2 entry:')==count and not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
   _,legacy=compile_one(name+'-disabled',model,source,['--mcs251-wide-backend'],opt=opt)
   _,old=compile_one(name+'-baseline',model,source,['--mcs251-wide-backend'],a.baseline,opt=opt);assert legacy==old
   bodies={n:asm.split('wide-mir accepted: '+n+';',1)[1].split('; mcs251 wide-mir',1)[0].split('; mcs251 wide-mir fallback:',1)[0] for n in accepted}
   for n,body in bodies.items():
    body=body.split(';--------------------------------------------------------',1)[0]
    assert bool(re.search(r'\tecall\t',body))==(n=='chain'),n
    frame=int(re.search(r'frame=(\d+)',body)[1]);occupied=set()
    for slot,size in re.findall(r'wide object \w+ slot=(\d+) size=(\d+)',body):
     slot=int(slot);size=int(size);region=set(range(slot,slot+size));assert slot>=0 and slot+size<=frame and occupied.isdisjoint(region);occupied|=region
    for width,slot in re.findall(r'wide v\d+ \S+ width=(\d+) reg=-1 slot=(\d+)',body):
     slot=int(slot);width=int(width);assert slot+width<=frame and occupied.isdisjoint(range(slot,slot+width))
   if corpus=='ordinary':
    assert re.findall(r'wide-mir fallback: (\w+)',asm)==[]
    for fn,width in [('zero',4),('byte_value',1),('word_value',2),('pointer_value',3),('float_value',4)]:assert 'wide ordinary ABI2 return physical='+str(width) in bodies[fn]
    assert set(map(int,re.findall(r'entry-offset=(-?\d+)',bodies['many'])))=={-4,-8,-9,-12,-14,-18,-19}
   if corpus=='ordinary-leaf':
    assert 'wide object a slot=0 size=4' in bodies['c_first'] and 'wide parameter address entry-offset=-6' in bodies['c_first']
    assert 'frame=0 ' in bodies['c_second'] and 'wide parameter address entry-offset=-6' in bodies['c_second']
    for fn in ['leaf_pointer','leaf_bits']:
     assert 'wide object p slot=0 size=3' in bodies[fn] and 'wide parameter address entry-offset=-5' in bodies[fn]
     assert set(map(int,re.findall(r'entry-offset=(-?\d+)',bodies[fn])))=={-5,-9}
    assert 'entry-offset=-6' in bodies['c_unused'] and '\tmov\ta,dpl' not in bodies['c_unused']
    assert '\tsub\tspx,#8\n\teret' in bodies['leaf_frame']
    assert '\tmov\ta,#0x80' in bodies['leaf_negative_zero'] and '\tmov\tdptr,#0x0000' in bodies['leaf_negative_zero']
    assert 'reg=-1' in bodies['c_pressure']
   rows.append(dict(model=model,opt=opt,corpus=corpus,accepted=accepted,deterministic=True,optInIsolation=True))
header='typedef unsigned long u32;typedef unsigned char u8;\n'
negative={
 'runtime-call':'float f(float a,float b) __reentrant {return a+b;}',
 'volatile-param':'u32 f(volatile u32 a) __reentrant {return a+a;}',
 'boolean':'_Bool f(u32 a) __reentrant {return a!=0;}',
 'ninth':'u32 f(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 g,u32 h,u32 i) __reentrant {return a+i;}',
 'critical':'u32 f(u32 a) __reentrant __critical {return a+1;}',
 'bank1':'u32 f(u32 a) __reentrant __using(1) {return a+1;}',
 'code-pointer':'u8 __code *f(u8 __code *a) __reentrant {return a;}',
 'explicit-reentrant-no-stack':'u32 f(u32 a,u32 b) __reentrant {return a+b;}',
 'nonreentrant':'u32 f(u32 a,u32 b) {return a+b;}',
}
for model in ['small','large']:
 for name,source in negative.items():
  asm,obj=compile_one(model+'-'+name,model,header+source,['--mcs251-wide-ordinary'],stack=name not in ['nonreentrant','explicit-reentrant-no-stack'])
  _,old=compile_one(model+'-'+name+'-old',model,header+source,[],a.baseline,stack=name not in ['nonreentrant','explicit-reentrant-no-stack'])
  if name=='explicit-reentrant-no-stack':
   assert 'wide-mir accepted: f;' in asm and 'wide immutable parameter home removed: a physical=4' in asm and 'wide object a ' not in asm
  else:assert 'wide-mir accepted:' not in asm and obj==old,(model,name)
  rows.append(dict(model=model,boundary=name,fallbackIdentical=name!='explicit-reentrant-no-stack',newlyAccepted=name=='explicit-reentrant-no-stack'))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ordinary leaf configurations/boundaries')
