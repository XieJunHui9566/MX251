"""Check physical pointer ABI widths, deterministic code, and conservative boundaries."""
from pathlib import Path
import argparse,subprocess,json,re,shutil
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--baseline-has-stack-pointers',action='store_true')
a=p.parse_args();T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def compile_one(name,compiler,part,model,opt,wide=True,source=None):
 d=a.output_dir/name;d.mkdir();shutil.copyfile(T/'wide-stack-pointers.h',d/'wide-stack-pointers.h')
 text=source if source is not None else (T/('wide-stack-pointer-'+part+'.c')).read_text()
 (d/'test.c').write_text(text)
 args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi','--opt-code-'+opt]
 if wide:args+=['--mcs251-wide-backend']
 q=subprocess.run(args+['-c','test.c'],cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr)
 assert not q.returncode,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  for part,count in [('callers',6),('callees',9)]:
   name=model+'-'+opt+'-'+part
   asm,obj=compile_one(name,a.sdcc,part,model,opt)
   assert len(re.findall('wide-mir accepted:',asm))==count and 'wide-mir fallback:' not in asm
   assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
   _,again=compile_one(name+'-repeat',a.sdcc,part,model,opt);assert obj==again
   _,old=compile_one(name+'-baseline',a.baseline,part,model,opt,False)
   oldasm,oldwide=compile_one(name+'-baseline-wide',a.baseline,part,model,opt,True)
   if a.baseline_has_stack_pointers:
    assert oldwide==obj and len(re.findall('wide-mir accepted:',oldasm))==count
   else:assert oldwide==old and 'wide-mir accepted:' not in oldasm
   _,legacy=compile_one(name+'-legacy',a.sdcc,part,model,opt,False);assert old==legacy
   if part=='callers':
    for n in [3,5,6,7,9,13,18]:assert 'bytes='+str(n)+'\n' in asm
    frames=dict((n,int(v)) for n,v in re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm))
    assert frames['call_pressure']>=16 and frames['call_memory']>=18
   else:
    for fn,offsets in [('ptr8',{-3,-6,-8,-11,-12,-15}),('many_ptr',{-5,-8,-11,-14,-17,-20})]:
     body=asm.split('wide-mir accepted: '+fn+';',1)[1].split('wide-mir accepted:',1)[0]
     assert set(map(int,re.findall(r'entry-offset=(-?\d+)',body)))==offsets
    body=asm.split('wide-mir accepted: volatile3;',1)[1]
    assert body.count('wide memory load width=1 volatile-bytes=1')==2 and body.count('\tmov\ta,@dr16\n')==2,body
   rows.append(dict(model=model,opt=opt,part=part,functions=count,deterministic=True,legacyUnchanged=True))
header='#include "wide-stack-pointers.h"\n'
negative={
 'volatile-pointer-parameter':'u32 f(u32 a,u32 b,u8 X * volatile p) ABI {return (u32)p+(u32)p+a+b;}',
 'addressed-pointer-parameter':'void g(u8 X **p) ABI;u32 f(u32 a,u32 b,u8 X *p) ABI {g(&p);return a+b+(u32)p;}',
 'ninth-parameter':'u32 f(u32 a,u32 b,u8 X *c,u8 X *d,u8 X *e,u8 X *f,u8 X *g,u8 X *h,u8 X *i) ABI {return a+b+(u32)i;}',
}
for model in ['small','large']:
 for name,s in negative.items():
  asm,obj=compile_one(model+'-'+name,a.sdcc,'',model,'speed',True,header+s)
  _,old=compile_one(model+'-'+name+'-old',a.baseline,'',model,'speed',False,header+s)
  if name=='addressed-pointer-parameter':
   assert 'wide-mir accepted: f;' in asm and 'wide parameter address entry-offset=-5' in asm and 'wide stack parameter width=3 result=4 entry-offset=-5' in asm
   rows.append(dict(model=model,boundary=name,physicalPointerSlotAccepted=True))
  else:
   assert 'wide-mir accepted:' not in asm and obj==old,(model,name)
   rows.append(dict(model=model,boundary=name,fallbackUnchanged=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'pointer ABI configurations/boundaries')
