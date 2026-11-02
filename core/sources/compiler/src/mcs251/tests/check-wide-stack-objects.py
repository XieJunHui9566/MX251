#!/usr/bin/env python3
"""Real stack-object layout, alias lifetime and conservative fallback checks."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 for auto in [False,True]:
  d=a.output_dir/(model+('-auto' if auto else '-reentrant'));d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--opt-code-speed','--mcs251-wide-backend','--mcs251-native-int-runtime']+(['--stack-auto'] if auto else [])+['-c','test.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(d/'test.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm);fallback=re.findall(r'wide-mir fallback: (\w+)',asm)
  if not auto:
   # 当前静态分配模式仍给导入器真实形参符号，须整体回退且保持机器码。
   assert not accepted and len(fallback)==14
   old=(d/'test.rel').read_bytes()
   q=subprocess.run([x for x in args if x!='--mcs251-wide-backend'],cwd=d,capture_output=True)
   (d/'fallback-off.log').write_bytes(q.stdout+q.stderr);assert not q.returncode and old==(d/'test.rel').read_bytes()
   rows.append(dict(model=model,stackAuto=False,fallback=fallback,fallbackMachineCodeIdentical=True));continue
  assert len(accepted)==13 and fallback==['s_too_big'],(accepted,fallback)
  for n in accepted:
   body=asm.split('wide-mir accepted: '+n+';',1)[1].split('; mcs251 wide-mir',1)[0]
   frame=int(re.search(r'frame=(\d+)',body)[1]);objects=[(int(s),int(z)) for s,z in re.findall(r'; wide object \S+ slot=(\d+) size=(\d+)',body)]
   values=[(int(i),int(z),int(g),int(s),int(e.replace(':',''),16)) for i,z,g,s,e in re.findall(r'; wide v(\d+) \S+ width=(\d+) reg=(-?\d+) slot=(-?\d+) edges=([0-9a-f:]+)',body)]
   slots=[(s,z) for i,z,g,s,e in values if g<0]
   occupied=set()
   for s,z in objects:
    region=set(range(s,s+z));assert region.isdisjoint(occupied) and s+z<=frame;occupied|=region
   for i,z,g,s,e in values:
    if g>=0:continue
    region=set(range(s,s+z));assert region.isdisjoint(occupied) and s>=0 and s+z<=frame
    for j,w,h,t,edges in values:
     if j<i and h<0 and e&(1<<j):assert region.isdisjoint(range(t,t+w)),(n,i,j)
   if objects:
    assert re.search(r'\tmov\tdr(?:0|4|12|16|24|28),spx',body) or '; wide stack direct object=' in body
    assert not re.search(r'\tejmp\t___m251r4_',body),'frame object must survive callees'
   if n=='s_two':assert len(objects)==2
   if n=='s_spill':assert objects and slots
  rows.append(dict(model=model,stackAuto=auto,accepted=accepted,fallback=fallback,objectsAndSpillsDisjoint=True,tailLifetimePreserved=True))
negative={
 'too_big':'u32 f(u32 n) ABI {u8 a[513];a[n&511]=(u8)n;return a[n&511];}',
 'frame_sum':'u32 f(u32 n) ABI {u8 a[300],b[300];a[n&255]=(u8)n;b[n&255]=(u8)(n>>8);return a[n&255]+b[n&255];}',
 'formal_address':'u32 sink(u32 *) ABI;u32 f(u32 n) ABI {return sink(&n);}',
 'legacy_call':'u32 sink(u8 *) __reentrant;u32 f(u32 n) ABI {u8 a[8];a[0]=(u8)n;return sink(a);}',
 'true_scalar':'u32 sink(u32 *) ABI;u32 f(u32 n) ABI {u32 a=n;sink(&a);return a;}',
}
header='typedef unsigned char u8;typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
for model in ['small','large']:
 for name,src in negative.items():
  objects=[]
  for wide in [False,True]:
   d=a.output_dir/f'{model}-{name}-{wide}';d.mkdir();(d/'test.c').write_text(header+src)
   args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-native-int-runtime']+(['--mcs251-wide-backend'] if wide else [])+['-c','test.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
   asm=(d/'test.asm').read_text()
   if name in ['legacy_call','true_scalar','formal_address'] and wide:
    assert 'wide-mir accepted: f;' in asm
    if name=='legacy_call':assert 'wide ordinary ABI call:' in asm and '\tecall\t_sink' in asm and '\tejmp\t_sink' not in asm
    elif name=='formal_address':assert 'wide local snapshot store' in asm and 'wide object n slot=0 size=4' in asm and '\tejmp\t___m251r4_sink' not in asm
    else:assert 'wide local snapshot store' in asm and 'wide local snapshot load' in asm
   else:assert 'wide-mir accepted' not in asm
   objects.append((d/'test.rel').read_bytes())
  if name in ['legacy_call','true_scalar','formal_address']:rows.append(dict(model=model,boundary=name,qualifiedPathAccepted=True))
  else:
   assert objects[0]==objects[1],(model,name)
   rows.append(dict(model=model,boundary=name,fallbackMachineCodeIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 2 stack-auto native configurations, 2 non-stack-auto and 10 boundary whole-function fallback checks')
