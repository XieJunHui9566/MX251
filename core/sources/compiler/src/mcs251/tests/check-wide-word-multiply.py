#!/usr/bin/env python3
"""Compile native narrow products, real spills, and full-width call boundaries."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
boundary='''typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u16 truncated(u16 a,u16 b) ABI {return a*b;}
long signed_full(int a,int b) ABI {return (long)a*b;}
long mixed(int a,u16 b) ABI {return (long)a*b;}
u32 wide(u32 a,u32 b) ABI {return a*b;}
u32 __fsmul_u16(u32,u32) ABI;
u32 user_call(u32 a,u32 b) ABI {return __fsmul_u16(a,b);}
'''
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();commands=[]
 def compile(name,source,wide=True):
  (d/(name+'.c')).write_text(source)
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-native-abi']+(['--mcs251-wide-backend'] if wide else [])+['-c',name+'.c']
  r=subprocess.run(args,cwd=d,capture_output=True);(d/(name+'.log')).write_bytes(r.stdout+r.stderr);assert not r.returncode,r.stderr
  commands.append(dict(args=args,exitCode=r.returncode));return (d/(name+'.asm')).read_text()
 asm=compile('core',a.source.read_text());accepted=dict(re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm))
 assert len(accepted)==13 and int(accepted['pressure'])>0
 assert 'wide-mir fallback:' not in asm and 'byteword' in accepted
 assert 'reg=24' in asm and 'reg=28' in asm
 assert re.search(r'mul\s+wr16,wr20\s+mov\s+@spx',asm), 'result spill coverage'
 assert re.search(r'mov\s+(wr\d+),@spx[^\n]*\n(?:\tmov[^\n]*\n){0,2}\tmov\s+wr20,\1\n; wide integer multiply',asm), 'input spill coverage'
 assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b',asm)
 assert asm.count('wide integer multiply 16x16 -> 32')==14 and asm.count('wide integer multiply 8x8 -> 16')==1
 assert {int(x) for x in re.findall(r'wide getword offset=(\d)',asm)}=={0,1,2}
 neg=compile('boundary',boundary)
 assert re.findall(r'wide-mir accepted: (\w+)',neg)==['truncated','signed_full','mixed','wide','user_call']
 assert neg.count('ecall\t__mullong')==1
 assert neg.count('wide word-origin summary products=1 ')==2
 assert 'wide integer multiply' not in neg and re.search(r'\b(?:ecall|ejmp)\s+___m251r4___fsmul_u16\b',neg)
 if a.baseline:
  old=d/'baseline';old.mkdir()
  for name in ('core','boundary'):
   (old/(name+'.c')).write_bytes((d/(name+'.c')).read_bytes())
   args=[str(a.baseline.resolve()),*commands[0]['args'][1:-1],name+'.c']
   q=subprocess.run(args,cwd=old,capture_output=True);assert q.returncode==0,q.stderr
   if name=='core':assert (old/(name+'.rel')).read_bytes()==(d/(name+'.rel')).read_bytes(),(model,name)
 rows.append(dict(model=model,accepted=accepted,commands=commands,hardwareExecuted=False,parentCoreObjectsIdentical=bool(a.baseline)))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS: 13 native functions/model, actual input/result spills, exact products/extracts, runtime-call boundaries')
