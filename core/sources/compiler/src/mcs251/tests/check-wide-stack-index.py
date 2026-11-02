"""Immutable frame-base rematerialization: modes, accesses and fallbacks."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
def compile_one(label,source,compiler,model,opt):
 d=a.output_dir/label;d.mkdir();(d/'test.c').write_text(source)
 args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-abi','--mcs251-wide-backend','-c','test.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(label,q.stderr)
 (d/'command.json').write_text(json.dumps(args,indent=2));return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
src=a.source.read_text()
accesses=lambda s:re.findall(r'; wide (?:memory|code|stack direct) (?:load|store)[^\n]*',s)
for model in ['small','large']:
 for opt in ['speed','size']:
  label=model+'-'+opt;old,x=compile_one(label+'-old',src,a.baseline,model,opt);new,y=compile_one(label+'-new',src,a.sdcc,model,opt)
  assert len(re.findall('wide-mir accepted:',new))==8 and re.findall(r'wide-mir fallback: (\w+)',new)==['si_s8','si_s16']
  assert accesses(old)==accesses(new),'Preserve memory order/width/volatile access strategy'
  assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',new)
  hits=re.findall(r'wide stack indexed base object=(\d+) offset=(\d+) op=([+-]) index-width=(\d+)',new)
  if opt=='speed':assert x==y and not hits
  else:
   assert len(hits)>10 and any(int(h[1])>0 for h in hits) and any(h[2]=='-' for h in hits) and any(h[3]=='4' for h in hits),hits
   for body in new.split('wide-mir accepted: ')[1:]:
    body=body.split('; mcs251 wide-mir',1)[0];frame=int(re.search(r'frame=(\d+)',body)[1]);occupied=set()
    for slot,size in re.findall(r'; wide object \S+ slot=(\d+) size=(\d+)',body):
     slot,size=int(slot),int(size);area=set(range(slot,slot+size));assert area.isdisjoint(occupied) and slot+size<=frame;occupied|=area
    for slot,width in re.findall(r'; wide v\d+ \S+ width=(\d+) reg=-1 slot=(\d+)',body):
     width,slot=int(slot),int(width);assert set(range(slot,slot+width)).isdisjoint(occupied) and slot+width<=frame
  _,again=compile_one(label+'-repeat',src,a.sdcc,model,opt);assert y==again
  rows.append(dict(model=model,opt=opt,nativeFunctions=8,fallbackFunctions=2,folds=len(hits),sameMemoryAccessSites=True,speedObjectUnchanged=(x==y),deterministic=True))
header=src[:src.index('u32 legacy_mutate')]
for model in ['small','large']:
 for name,nextname in [('si_s8','si_s16'),('si_s16','si_s32')]:
  code=header+src[src.index('u32 '+name+'('):src.index('u32 '+nextname+'(')]
  _,x=compile_one(model+'-'+name+'-old',code,a.baseline,model,'size');asm,y=compile_one(model+'-'+name+'-new',code,a.sdcc,model,'size')
  assert x==y and 'wide-mir accepted' not in asm
  rows.append(dict(model=model,boundary=name,unchangedMachineCode=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS4 mode/model configurations,8 native +2 fallback functions;4 narrow-index fallback objects unchanged')
