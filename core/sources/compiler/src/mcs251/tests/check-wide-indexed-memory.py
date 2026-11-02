"""Single-use address folding: real spills, access preservation, excluded spaces."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
def compile_one(name,source,compiler,model,opt):
 d=a.output_dir/name;d.mkdir();(d/'test.c').write_text(source)
 args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-abi','--mcs251-wide-backend','-c','test.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  source=a.source.read_text();old,_=compile_one(model+'-'+opt+'-old',source,a.baseline,model,opt);new,obj=compile_one(model+'-'+opt+'-new',source,a.sdcc,model,opt)
  assert len(re.findall('wide-mir accepted:',new))==18 and 'wide-mir fallback' not in new
  accesses=lambda s:re.findall(r'; wide (?:memory|code) (?:load|store) width=\d volatile-bytes=\d',s)
  assert accesses(old)==accesses(new),'Access order and widths must not change'
  folds=new.count('wide indexed address');assert folds>10
  assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',new)
  for part in new.split('; wide indexed address ')[1:]:assert '\tanl\twr16,#0x00ff' in part.split('; wide memory ',1)[0]
  frames={n:int(f) for n,f in re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',new)}
  assert frames['spill']>0 and frames['local']>=16
  _,again=compile_one(model+'-'+opt+'-repeat',source,a.sdcc,model,opt);assert obj==again
  rows.append(dict(model=model,opt=opt,nativeFunctions=18,folds=folds,frames=frames,accessSitesIdenticalToBaseline=True,deterministic=True))
header='typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
negative={
 'volatile8':'u32 f(u32 a,u32 b) ABI {return ((volatile u8 __xdata *)a)[b&63];}',
 'volatile16':'u32 f(u32 a,u32 b) ABI {return ((volatile u16 __xdata *)a)[b&31];}',
 'volatile32':'u32 f(u32 a,u32 b) ABI {return ((volatile u32 __xdata *)a)[b&15];}',
 'code':'extern const __code u8 data[128];u32 f(u32 a,u32 b) ABI {return data[b&127];}',
 'return-pointer':'u8 __xdata *f(u8 __xdata *p,u16 n) ABI {return p+n;}',
 'escaped-pointer':'u32 sink(u8 __xdata *) ABI;u32 f(u32 a,u32 b) ABI {u8 __xdata *p=(u8 __xdata *)a+(b&63);u32 v=sink(p);return *p+v;}',
}
for model in ['small','large']:
 for n,s in negative.items():
  old,x=compile_one(model+'-'+n+'-old',header+s,a.baseline,model,'speed')
  new,y=compile_one(model+'-'+n+'-new',header+s,a.sdcc,model,'speed')
  assert 'wide indexed address' not in new and x==y,(model,n)
  rows.append(dict(model=model,boundary=n,unchangedMachineCode=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS4 indexed configurations and12 unchanged boundary objects')
