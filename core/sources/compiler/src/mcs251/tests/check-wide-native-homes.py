from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,src,flags,compiler=None,opt='speed'):
 d=a.output_dir/name;d.mkdir();(d/'wide-ordinary-calls.h').write_bytes((T/'wide-ordinary-calls.h').read_bytes());(d/'test.c').write_text(src)
 cmd=[str((compiler or a.sdcc).resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--mcs251-native-abi',*flags,'-c','test.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd));assert q.returncode==0,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  name=model+'-'+opt;src=(T/'wide-native-homes.c').read_text()
  asm,obj=build(name,model,src,['--mcs251-wide-ordinary'],opt=opt)
  _,again=build(name+'-repeat',model,src,['--mcs251-wide-ordinary'],opt=opt);assert obj==again
  old,_=build(name+'-old',model,src,['--mcs251-wide-ordinary'],a.baseline,opt)
  _,off=build(name+'-off',model,src,[],opt=opt);_,oldoff=build(name+'-old-off',model,src,[],a.baseline,opt);assert off==oldoff
  accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==10 and 'wide-mir fallback:' not in asm,accepted
  for fn in accepted:
   body=asm.split('wide-mir accepted: '+fn+';',1)[1].split('; mcs251 wide-mir',1)[0].split(';--------------------------------------------------------',1)[0]
   if fn in ['native_mutable','native_addressed']:
    assert 'wide object a ' in body and 'wide object b ' in body and 'wide local snapshot store' in body,(fn,body)
   else:
    assert not re.search(r'wide object (a|b|p|q) ',body),(fn,body)
   if fn=='native_neighbor':assert 'wide object guard slot=0 size=3' in body and 'volatile-bytes=1' in body
   if fn=='native_pointee':assert body.count('wide memory load width=4 volatile-bytes=1')==3 and body.count('wide memory store width=4 volatile-bytes=1')==1
  assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
  rows.append(dict(model=model,opt=opt,accepted=accepted,previousAccepted=re.findall(r'wide-mir accepted: (\w+)',old),immutablePromoted=True,mutableAndAddressedRetained=True,volatileTransactionsPreserved=True,optionOffIdentical=True,deterministic=True))
negative={
 'volatile-first':'u32 f(volatile u32 a,u32 b) ABI {return a+b;}',
 'volatile-second':'u32 f(u32 a,volatile u32 b) ABI {return a+b;}',
 'volatile-pointer':'u32 f(u32 a,u32 PTR * volatile p) ABI {return a+*p;}',
 'ninth':'u32 f(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 g,u32 h,u32 i) ABI {return a+i;}',
}
for model in ['small','large']:
 for name,src in negative.items():
  src='#include "wide-ordinary-calls.h"\n'+src
  asm,obj=build(model+'-'+name,model,src,['--mcs251-wide-ordinary'])
  old,oldobj=build(model+'-'+name+'-old',model,src,['--mcs251-wide-ordinary'],a.baseline)
  assert 'wide-mir accepted:' not in asm and obj==oldobj,(model,name)
  rows.append(dict(model=model,boundary=name,fallbackIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'native home configurations/boundaries')
