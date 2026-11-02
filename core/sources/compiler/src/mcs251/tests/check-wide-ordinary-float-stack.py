from pathlib import Path
import argparse,subprocess,re,json
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,source,flags,compiler=None,opt='speed'):
 d=a.output_dir/name;d.mkdir();(d/'wide-ordinary-calls.h').write_bytes((T/'wide-ordinary-calls.h').read_bytes());(d/'test.c').write_text(source)
 cmd=[str((compiler or a.sdcc).resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,*flags,'-c','test.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd));assert q.returncode==0,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  name=model+'-'+opt;source=(T/'wide-ordinary-float-stack.c').read_text()
  asm,obj=build(name,model,source,['--mcs251-wide-ordinary'],opt=opt)
  _,again=build(name+'-repeat',model,source,['--mcs251-wide-ordinary'],opt=opt);assert obj==again
  _,off=build(name+'-off',model,source,['--mcs251-wide-backend'],opt=opt)
  _,oldoff=build(name+'-old-off',model,source,['--mcs251-wide-backend'],a.baseline,opt=opt);assert off==oldoff
  old,oldobj=build(name+'-old',model,source,['--mcs251-wide-ordinary'],a.baseline,opt=opt)
  accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==6 and 'wide-mir fallback:' not in asm
  assert asm.count('wide stack float call migration:')==6
  if 'wide-mir accepted:' in old:
   assert re.findall(r'wide-mir accepted: (\w+)',old)==accepted
   assert 'wide ordinary argument direct' in asm
   if 'wide ordinary argument direct' in old:
    assert obj==oldobj, 'This non-forwarding corpus must remain byte-identical to R82'
  for fn in accepted:
   body=asm.split('wide-mir accepted: '+fn+';',1)[1].split('; wide stack float call migration:',1)[0]
   assert body.count('; wide memory ')==body.count('; wide stack direct ')
  body=asm.split('wide-mir accepted: volatile_bits;',1)[1].split('; wide stack float call migration:',1)[0]
  memories=re.findall(r'; wide memory (load|store) width=4 volatile-bytes=(\d)\n; wide stack direct object=(\d+) offset=(\d+)\n(.*?)(?=; wide |\tecall|\teret)',body,re.S)
  assert len(memories)>=6
  for kind,volatile,ob,offset,code in memories:
   assert volatile=='1'
   addresses=re.findall(r'(?:mov\ta,(@spx[^\n]*)|mov\t(@spx[^,]*),a)',code)
   assert len(addresses)==4,(kind,code)
  assert '\tecall\t_mutate' in asm and 'reg=-1' in asm
  rows.append(dict(model=model,opt=opt,accepted=accepted,deterministic=True,optionOffIdentical=True,volatileFourByteTransactions=True))
header='#include "wide-ordinary-calls.h"\n'
negative={
 'conditional-alias':(T/'wide-ordinary-float-stack-branch.c').read_text(),
 'abi4':'u32 f(u32 a,u32 b) ABI {Bits x,y,z;x.u=a;y.u=b;z.f=old_float(x.f,y.f,1);return z.u;}',
 'external-float':'u32 f(float PTR *p,u32 a) OLD {Bits x,y;x.u=a;y.f=old_float(*p,x.f,1);return y.u;}',
 'runtime':'u32 f(u32 a,u32 b) OLD {Bits x,y,z;x.u=a;y.u=b;z.f=x.f+y.f;return z.u;}',
}
for model in ['small','large']:
 for name,source in negative.items():
  asm,obj=build(model+'-'+name,model,header+source,['--mcs251-wide-ordinary'])
  _,old=build(model+'-'+name+'-old',model,header+source,['--mcs251-wide-ordinary'],a.baseline)
  assert 'wide-mir accepted:' not in asm and obj==old,(model,name)
  rows.append(dict(model=model,boundary=name,fallbackIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ordinary direct-stack float CALL configurations/boundaries')
