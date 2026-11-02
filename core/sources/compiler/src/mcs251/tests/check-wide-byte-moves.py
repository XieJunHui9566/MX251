"""Verify byte returns, Boolean movement, access order and unchanged fallback."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);source=a.source.read_text();rows=[]
def compile_one(name,src,compiler,model,opt):
 d=a.output_dir/name;d.mkdir();(d/'test.c').write_text(src)
 args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-abi','--mcs251-wide-backend','-c','test.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
def part(text,name):return text.split('wide-mir accepted: '+name+';',1)[1].split('; wide allocation ',1)[0]
for model in ['small','large']:
 for opt in ['speed','size']:
  old,_=compile_one(model+'-'+opt+'-old',source,a.baseline,model,opt);new,obj=compile_one(model+'-'+opt+'-new',source,a.sdcc,model,opt)
  assert len(re.findall('wide-mir accepted:',new))==10 and 'wide-mir fallback' not in new
  accesses=lambda s:re.findall(r'; wide (?:memory|code) (?:load|store) width=\d volatile-bytes=\d',s)
  assert accesses(old)==accesses(new) and 'volatile-bytes=1' in new
  assert len(re.findall(r'; wide byte boolean',new))>=6 and '; wide byte truth' in new and '; wide byte copy' in new
  for n in ['r_unsigned','r_signed']:
   assert re.search(r'\tmov[zs]\twr6,',part(old,n))
   assert not re.search(r'\tmov[zs]\twr6,',part(new,n)) and '\teret' in part(new,n)
  for n in ['r_old_returns','r_new_returns']:
   assert part(new,n).count('\tecall\t')==2
   assert '\tmov\twr10,wr6' not in part(new,n)
  assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',new)
  _,again=compile_one(model+'-'+opt+'-repeat',source,a.sdcc,model,opt);assert obj==again
  rows.append(dict(model=model,opt=opt,nativeFunctions=10,byteReturnExtensionRemoved=True,byteCallResultsDirect=True,accessSitesPreserved=True,deterministic=True))
# 位测试触发的前端形式本轮未扩展；旧路径必须继续可用并保持原目标文件。
excluded=source[:source.index('u8 legacy_u8')]+source[source.index('u32 r_byte_assign'):].replace('if(y)x=z','if(y&1)x=z')
for model in ['small','large']:
 old,x=compile_one(model+'-fallback-old',excluded,a.baseline,model,'speed');new,y=compile_one(model+'-fallback-new',excluded,a.sdcc,model,'speed')
 assert 'wide-mir fallback: r_byte_assign' in old and 'wide-mir fallback: r_byte_assign' in new and x==y
 rows.append(dict(model=model,excludedBitTest=True,unchangedMachineCode=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS4 byte-move configurations and2 unchanged bit-test fallback objects')
