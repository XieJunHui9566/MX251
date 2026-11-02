from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,opt,flag,compiler):
 d=a.output_dir/name;d.mkdir()
 for fn in ['wide-ordinary.h','wide-ordinary-transfer.c']:(d/fn).write_bytes((T/fn).read_bytes())
 cmd=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,flag,'-c','wide-ordinary-transfer.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd));assert q.returncode==0,(name,q.stderr)
 return (d/'wide-ordinary-transfer.asm').read_text(),(d/'wide-ordinary-transfer.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  key=model+'-'+opt
  asm,obj=build(key,model,opt,'--mcs251-wide-ordinary',a.sdcc)
  _,repeat=build(key+'-repeat',model,opt,'--mcs251-wide-ordinary',a.sdcc);assert obj==repeat
  old,_=build(key+'-old',model,opt,'--mcs251-wide-ordinary',a.baseline)
  _,off=build(key+'-off',model,opt,'--mcs251-wide-backend',a.sdcc)
  _,oldoff=build(key+'-old-off',model,opt,'--mcs251-wide-backend',a.baseline);assert off==oldoff
  assert re.findall(r'wide-mir accepted: (\w+)',asm)==re.findall(r'wide-mir accepted: (\w+)',old)
  assert re.findall(r'wide-mir fallback: (\w+)',asm)==['c_float']
  literals=set(map(int,re.findall(r'wide ordinary argument direct literal physical=(\d)',asm)));assert literals=={1,2,3,4},literals
  widths={kind:set(map(int,re.findall(r'wide ordinary '+kind+r' direct(?: words)? reg=\d+ physical=(\d)',asm))) for kind in ['argument','result']}
  assert widths['argument']=={1,2,3,4} and widths['result']=={1,2,3,4},widths
  assert 'wide ordinary result direct words reg=' in asm and 'wide ordinary argument direct words reg=' in asm
  assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
  for kind,words,reg,width in re.findall(r'wide ordinary (argument|result) direct( words)? reg=(\d+) physical=(\d)',asm):
   reg=int(reg);width=int(width);physical=4 if width==3 else width
   assert ((1<<physical)-1)<<reg & (3<<10)==0
   if words:assert reg>=16 and width>1
   else:assert reg+physical<=16
  assert 'reg=-1' in asm and '@spx' in asm,'Spill path must still be exercised'
  body=asm.split('wide-mir accepted: c_pointer;',1)[1].split('; mcs251 wide-mir',1)[0]
  assert 'wide ordinary result direct' in body and 'physical=3' in body
  rows.append(dict(model=model,opt=opt,literalWidths=sorted(literals),argumentWidths=sorted(widths['argument']),resultWidths=sorted(widths['result']),highWords=True,spillsRetained=True,optionOffIdentical=True,deterministic=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ordinary transfer configurations')
