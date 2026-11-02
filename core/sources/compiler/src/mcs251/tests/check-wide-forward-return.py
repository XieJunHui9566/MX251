from pathlib import Path
import argparse,json,subprocess,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,opt,stack,flag,cc):
 d=a.output_dir/name;d.mkdir()
 for fn in ['wide-forward.h','wide-forward-callers.c']:(d/fn).write_bytes((T/fn).read_bytes())
 cmd=[str(cc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--mcs251-native-abi',flag,*(['--stack-auto'] if stack else []),'-c','wide-forward-callers.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd));assert q.returncode==0,(name,q.stderr)
 return (d/'wide-forward-callers.asm').read_text(),(d/'wide-forward-callers.rel').read_bytes()
def body(asm,fn):return asm.split('wide-mir accepted: '+fn+';',1)[1].split('; mcs251 wide-mir',1)[0]
def instructions(asm,fn):return [x for x in body(asm,fn).splitlines()[2:] if x.startswith('\t')]
positives={'f_byte':1,'f_word':2,'f_dword':4,'f_float':4,'f_pointer':3,'f_local':4,'f_formal':4,'f_volatile':4,'f_recur':4,'f_many':4}
negative=['c_pair','c_native','negative_add','negative_store','negative_native_call','negative_native_entry']
for model in ['small','large']:
 for opt in ['speed','size']:
  for stack in [True,False]:
   key=model+'-'+opt+('-stack' if stack else '-explicit')
   asm,obj=build(key,model,opt,stack,'--mcs251-wide-ordinary',a.sdcc)
   _,repeat=build(key+'-repeat',model,opt,stack,'--mcs251-wide-ordinary',a.sdcc);assert obj==repeat
   old,oldobj=build(key+'-old',model,opt,stack,'--mcs251-wide-ordinary',a.baseline)
   _,off=build(key+'-off',model,opt,stack,'--mcs251-wide-backend',a.sdcc)
   _,oldoff=build(key+'-old-off',model,opt,stack,'--mcs251-wide-backend',a.baseline);assert off==oldoff
   assert re.findall(r'wide-mir accepted: (\w+)',asm)==re.findall(r'wide-mir accepted: (\w+)',old)
   assert 'wide ordinary result forwarded' in old
   assert 'wide repeated parameter loads reused=1' in body(asm,'f_recur')
   assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
   frames={}
   for fn,width in positives.items():
    b=body(asm,fn);m=re.search(r'wide ordinary result forwarded physical=(\d) frame=(\d+)\n((?:\tsub\tspx,#\d+\n)?)\teret',b);assert m,(key,fn)
    assert int(m[1])==width and '\tecall\t' in b
    frame=int(m[2]);assert m[3]==('\tsub\tspx,#%d\n'%frame if frame else '')
    frames[fn]=frame
    if fn in ['f_local','f_formal','f_volatile','f_many']:
     assert frame>0 and ('\tadd\tspx,#%d'%frame) in b
     assert b.index('\tecall\t')<b.index(m[0]) and not re.search(r'\tejmp\t_(?!_wm)',b)
   for fn in negative:
    assert 'wide ordinary result forwarded' not in body(asm,fn)
    assert instructions(asm,fn)==instructions(old,fn),(key,fn)
   rows.append(dict(model=model,opt=opt,stackAuto=stack,physicalWidths=[1,2,3,4],forwardingFunctions=list(positives),frames=frames,negativeFunctions=negative,negativeInstructionsIdentical=True,optionOffIdentical=True,deterministic=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'forward return configurations')
