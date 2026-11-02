from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,source,flags,compiler=None,opt='speed'):
 d=a.output_dir/name;d.mkdir();(d/'wide-ordinary-calls.h').write_bytes((T/'wide-ordinary-calls.h').read_bytes());(d/'test.c').write_text(source)
 cmd=[str((compiler or a.sdcc).resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,*flags,'-c','test.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd));assert q.returncode==0,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  name=model+'-'+opt;src=(T/'wide-immutable-home.c').read_text()
  asm,obj=build(name,model,src,['--mcs251-wide-ordinary'],opt=opt)
  _,again=build(name+'-repeat',model,src,['--mcs251-wide-ordinary'],opt=opt);assert obj==again
  old,_=build(name+'-old',model,src,['--mcs251-wide-ordinary'],a.baseline,opt=opt)
  _,off=build(name+'-off',model,src,[],opt=opt);_,oldoff=build(name+'-old-off',model,src,[],a.baseline,opt=opt);assert off==oldoff
  names=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(names)==11 and 'wide-mir fallback:' not in asm
  assert asm.count('wide immutable parameter home removed:')==9
  for fn in names:
   body=asm.split('wide-mir accepted: '+fn+';',1)[1].split('; mcs251 wide-mir',1)[0].split(';--------------------------------------------------------',1)[0]
   if fn in ['mutable_input','addressed_input']:assert 'wide object a slot=' in body and 'wide local snapshot load' in body
   else:assert 'wide object a ' not in body and 'wide object p ' not in body
   if fn=='neighboring_object':assert 'wide object guard slot=0 size=3' in body and 'volatile-bytes=1' in body
   if fn=='across_calls':assert body.count('\tecall\t_old_pair')==2
  rows.append(dict(model=model,opt=opt,accepted=names,removedHomes=9,mutableAndAddressedHomesRetained=True,neighboringObjectRepacked=True,optionOffIdentical=True,deterministic=True))
header='#include "wide-ordinary-calls.h"\n'
negative={
 'mutable':'u32 f(u32 a,u32 b) OLD {while(b&3U){a+=b;b--;}return a;}',
 'addressed':'u32 f(u32 a,u32 b) OLD {mutate(&a,&a,b);return a;}',
 'volatile':'u32 f(volatile u32 a) OLD {return a+a;}',
 'pointer-volatile':'u32 f(u32 PTR * volatile p) OLD {return *p;}',
 'external-float':'u32 f(volatile float PTR *p,u32 a) OLD {Bits b;b.f=old_float(*p,*p,(u8)a);return b.u;}',
 'abi4':'u32 f(u32 a,u32 b) ABI {return a+b;}',
}
for model in ['small','large']:
 for name,src in negative.items():
  flags=['--mcs251-native-abi','--mcs251-wide-ordinary']
  asm,obj=build(model+'-'+name,model,header+src,flags)
  old,oldobj=build(model+'-'+name+'-old',model,header+src,flags,a.baseline)
  if name=='abi4':
   assert 'wide-mir accepted: f;' in asm and 'wide-mir accepted: f;' in old and obj==oldobj
   assert asm.count('wide immutable parameter home removed:')==2
   rows.append(dict(model=model,newlyAccepted=name,removedHomes=2));continue
  if name=='addressed':
   assert 'wide object a slot=0 size=4' in asm and 'wide object a slot=0 size=4' in old
   assert 'wide ordinary argument direct' in asm and 'wide immutable parameter home removed:' not in asm
   rows.append(dict(model=model,boundary=name,homePreserved=True,callTransportOptimized=True));continue
  assert obj==oldobj,(model,name)
  assert 'wide-mir accepted:' not in asm or 'wide immutable parameter home removed:' not in asm,(model,name)
  rows.append(dict(model=model,boundary=name,objectIdentical=True,noPromotionInEmittedCode=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'immutable home configurations/boundaries')
