"""Byte compare/bitwise lowering: aliases, real spills, access order and exclusion."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
def compile_one(name,source,compiler,model,opt):
 d=a.output_dir/name;d.mkdir();(d/'test.c').write_text(source)
 args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-abi','--mcs251-wide-backend','-c','test.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
source=a.source.read_text()+'\n'+(a.source.parent/'wide-byte-pressure.c').read_text().split('u32 b_pressure',1)[1];source=source.replace('\n(u32 address','\nu32 b_pressure(u32 address')
assert 'u32 b_pressure(' in source
for model in ['small','large']:
 for opt in ['speed','size']:
  old,_=compile_one(model+'-'+opt+'-old',source,a.baseline,model,opt);new,obj=compile_one(model+'-'+opt+'-new',source,a.sdcc,model,opt)
  assert len(re.findall('wide-mir accepted:',new))==34 and 'wide-mir fallback' not in new
  accesses=lambda s:re.findall(r'; wide (?:memory|code) (?:load|store) width=\d volatile-bytes=\d',s)
  assert accesses(old)==accesses(new),'All volatile/ordinary access sites retain order and widths'
  assert new.count('wide byte compare signed=1')>=12 and new.count('wide byte bitwise')>20
  assert re.search(r'mov\ta,@spx[^\n]*\n\tmov\tr10,a\n\tmov\ta,@spx[^\n]*\n; wide byte compare',new)
  assert re.search(r'; wide byte bitwise[^\n]*\n\t(?:xrl|orl|anl)\tr11,[^\n]*\n\tmov\t@spx',new)
  assert len(re.findall(r'cmp\tr\d+,#0x',new))>=6
  assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',new)
  _,again=compile_one(model+'-'+opt+'-repeat',source,a.sdcc,model,opt);assert obj==again
  rows.append(dict(model=model,opt=opt,nativeFunctions=34,deterministic=True,accessSitesPreserved=True,bothSpillCompare=True,spilledBitwiseResult=True,literalCompare=True))
header='typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;typedef signed long s32;\n#define ABI __reentrant __sdcccall(4)\n'
negative={
 'compare16':'u16 f(u16 a,u16 b) ABI {return a<b;}',
 'compare32':'u16 f(s32 a,s32 b) ABI {return a>b;}',
 'bitwise16':'u16 f(u16 a,u16 b) ABI {return (a^b)&(a|0x8512);}',
 'bitwise32':'u32 f(u32 a,u32 b) ABI {return (a^b)&(a|0x85215678UL);}',
 'mixed-compare':'u16 f(s16 a,u16 b) ABI {return a<b;}',
 'float-compare':'u16 f(float a,float b) ABI {return a<b;}',
}
for model in ['small','large']:
 for n,s in negative.items():
  old,x=compile_one(model+'-'+n+'-old',header+s,a.baseline,model,'speed');new,y=compile_one(model+'-'+n+'-new',header+s,a.sdcc,model,'speed')
  # R64可改变宽比较产生的字节布尔临时值；保留宽比较本身与浮点运行库调用检查。
  # 此处是结构门槛，宽比较的结果另由独立参考实板检查，不用它替代语义验证。
  widecmp=lambda s:re.findall(r'\tcmp\t(?:wr|dr)[^\n]+',s)
  calls=lambda s:re.findall(r'\tecall\t[^\n]+',s)
  assert widecmp(old)==widecmp(new) and calls(old)==calls(new),(model,n)
  if n.startswith('bitwise'):assert x==y,(model,n)
  rows.append(dict(model=model,boundary=n,unchangedMachineCode=x==y,wideComparisonsPreserved=True,runtimeCallsPreserved=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS4 byte configurations/34 functions and12 wider/floating boundary checks')
