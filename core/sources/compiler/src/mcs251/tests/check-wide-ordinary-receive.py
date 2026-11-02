from pathlib import Path
import subprocess,argparse,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,src,flags,compiler=None,stack=False,opt='speed',atomicRejected=False):
 d=a.output_dir/name;d.mkdir();(d/'wide-ordinary-calls.h').write_bytes((T/'wide-ordinary-calls.h').read_bytes());(d/'test.c').write_text(src)
 cmd=[str((compiler or a.sdcc).resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,*(['--stack-auto'] if stack else []),*flags,'-c','test.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd))
 if atomicRejected:
  assert q.returncode!=0 and b'error 277: atomics are not supported' in q.stderr
  return '',b'error 277: atomics are not supported'
 assert q.returncode==0,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  src=(T/'wide-ordinary-receive.c').read_text();name=model+'-'+opt
  asm,obj=build(name,model,src,['--mcs251-wide-ordinary'],opt=opt)
  _,again=build(name+'-repeat',model,src,['--mcs251-wide-ordinary'],opt=opt);assert obj==again
  old,_=build(name+'-old',model,src,['--mcs251-wide-ordinary'],a.baseline,opt=opt)
  off,offobj=build(name+'-off',model,src,[],opt=opt)
  _,oldoff=build(name+'-old-off',model,src,[],a.baseline,opt=opt);assert offobj==oldoff
  accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert all(n in accepted for n in ['byte_home','word_home','long_home','float_home','pointer_home','alias_home','loop_home','qualified_pointee','unused_home']),accepted
  assert 'wide-mir accepted: qualified_pointee;' in old
  for fn,width in [('byte_home',1),('word_home',2),('long_home',4),('float_home',4),('pointer_home',3),('qualified_pointee',3)]:
   body=asm.split('wide-mir accepted: '+fn+';',1)[1].split('; mcs251 wide-mir',1)[0].split(';--------------------------------------------------------',1)[0]
   if fn in ['float_home','qualified_pointee']:
    assert 'wide object ' not in body and 'wide local snapshot ' not in body,(fn,body)
   else:
    assert re.search(r'wide object \w+ slot=\d+ size='+str(width),body),(fn,body)
    assert 'wide local snapshot store' in body and 'wide local snapshot load' in body
  body=asm.split('wide-mir accepted: qualified_pointee;',1)[1].split('; mcs251 wide-mir',1)[0].split(';--------------------------------------------------------',1)[0]
  assert body.count('wide memory store width=4 volatile-bytes=1')==1 and body.count('wide memory load width=4 volatile-bytes=1')==2
  assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
  rows.append(dict(model=model,opt=opt,accepted=accepted,oldAccepted=re.findall(r'wide-mir accepted: (\w+)',old),mutableHomesPreserved=True,immutableHomesRemoved=True,volatilePointeeTransactions=True,deterministic=True,optionOffIdentical=True))
negative={
 'volatile-scalar':'u32 f(volatile u32 a) OLD {return a+a;}',
 'volatile-pointer':'u32 f(u32 PTR * volatile p) OLD {return *p;}',
 'atomic-scalar':'u32 f(_Atomic u32 a) OLD {return a;}',
 'abi4-true-symbol':'u32 f(u32 a,u32 b) ABI {return a+b;}',
 'nonreentrant':'u32 f(u32 a,u32 b) {return a+b;}',
 'ninth':'u32 f(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 g,u32 h,u32 i) OLD {return a+i;}',
}
for model in ['small','large']:
 for name,src in negative.items():
  src='#include "wide-ordinary-calls.h"\n'+src
  asm,obj=build(model+'-'+name,model,src,['--mcs251-native-abi','--mcs251-wide-ordinary'],atomicRejected=name=='atomic-scalar')
  old,oldobj=build(model+'-'+name+'-old',model,src,['--mcs251-native-abi','--mcs251-wide-ordinary'],a.baseline,atomicRejected=name=='atomic-scalar')
  if name=='abi4-true-symbol':
   assert 'wide-mir accepted: f;' in asm and 'wide-mir accepted: f;' in old and obj==oldobj
   rows.append(dict(model=model,newlyAccepted=name,previousAccepted=True));continue
  assert 'wide-mir accepted:' not in asm and obj==oldobj,(model,name)
  rows.append(dict(model=model,boundary=name,fallbackIdentical=name!='atomic-scalar',diagnosticIdentical=name=='atomic-scalar'))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ordinary real-symbol RECEIVE configurations/boundaries')
