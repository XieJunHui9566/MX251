#!/usr/bin/env python3
"""Check byte selection, spilled slices, ABI byte results and volatile access width."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();commands=[]
 def compile(name,src):
  (d/(name+'.c')).write_text(src)
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-native-abi','--mcs251-wide-backend','-c',name+'.c']
  r=subprocess.run(args,cwd=d,capture_output=True);(d/(name+'.log')).write_bytes(r.stdout+r.stderr);commands.append(dict(args=args,exitCode=r.returncode));assert not r.returncode,r.stderr
  return (d/(name+'.asm')).read_text()
 asm=compile('core',a.source.read_text());accepted=dict(re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm))
 assert len(accepted)==30 and 'wide-mir fallback' not in asm
 paths={k:asm.count('wide getbyte path='+k) for k in ['byte','upper','spill']};assert all(paths.values())
 slots=[tuple(map(int,x)) for x in re.findall(r'getbyte path=spill slot=(\d+) index=(\d+)',asm)]
 assert {i for s,i in slots}=={0,1,2,3} and {(s+i)%2 for s,i in slots}=={0,1}
 assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',asm)
 for name in ['extract_0','extract_8','extract_16','extract_24','signed_source_0','signed_source_8','signed_source_16','signed_source_24']:
  body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
  assert not re.search(r'\b(?:srl|sra)\b',body),name
 # 返回类型8位，检查真正ABI字节入口；32/16/8位输入都覆盖。
 tiny=''.join('unsigned char f%d(unsigned long x) __reentrant __sdcccall(4){return (unsigned char)(x>>%d);}\n'%(i,i*8) for i in range(4))
 tiny+='unsigned char f4(unsigned int x) __reentrant __sdcccall(4){return (unsigned char)(x>>8);}\nunsigned char f5(unsigned char x) __reentrant __sdcccall(4){return x;}\n'
 ta=compile('tiny',tiny);assert len(re.findall('wide-mir accepted:',ta))==6 and 'wide-mir fallback' not in ta
 fallback=compile('volatile_fallback','unsigned char f(volatile unsigned long __xdata *p) __reentrant __sdcccall(4) {return (unsigned char)(*p>>24);}')
 assert 'wide-mir fallback: f' in fallback and len(re.findall(r'mov\s+a,@dpx\b',fallback))==4
 volatile=compile('volatile','unsigned long f(unsigned long a,unsigned long b) __reentrant __sdcccall(4) {volatile unsigned long __xdata *p=(volatile unsigned long __xdata *)a;unsigned long x=*p;return (unsigned char)(x>>24);}')
 assert 'wide-mir accepted: f;' in volatile and 'wide memory load width=4 volatile-bytes=1' in volatile
 assert len(re.findall(r'mov\s+a,@dr16\b',volatile))==4 and not re.search(r'mov\s+wr\d+,@dr16\b',volatile)
 repeated=compile('volatile_twice','unsigned long f(unsigned long a,unsigned long b) __reentrant __sdcccall(4) {volatile unsigned long __xdata *p=(volatile unsigned long __xdata *)a;unsigned long x=*p,y=*p;return (unsigned char)(x>>8)+(unsigned char)(y>>24);}')
 assert 'wide-mir accepted: f;' in repeated and len(re.findall(r'mov\s+a,@dr16\b',repeated))==8
 rows.append(dict(model=model,accepted=accepted,paths=paths,spillSlices=slots,byteAbiFunctions=6,volatileBytesRead=4,repeatedVolatileBytesRead=8,commands=commands,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS: 30 functions/model, all byte slices, upper registers and spills, byte ABI, ordered 4/8 volatile byte reads')
