"""ABI4 stack arguments: bounded imports, native/legacy boundary, and stack layout."""
from pathlib import Path
import argparse, subprocess, json, re

p=argparse.ArgumentParser()
p.add_argument('--sdcc',type=Path,required=True)
p.add_argument('--source-dir',type=Path,default=Path(__file__).resolve().parent)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]

def compile_one(name,source,model,opt,wide=True):
 d=a.output_dir/name;d.mkdir();(d/'test.c').write_text(source)
 args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi','--opt-code-'+opt]
 if wide:args+=['--mcs251-wide-backend']
 q=subprocess.run(args+['-c','test.c'],cwd=d,capture_output=True)
 (d/'compile.log').write_bytes(q.stdout+q.stderr)
 assert not q.returncode,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()

for model in ['small','large']:
 for opt in ['speed','size']:
  asm={}
  for part,count in [('callers',18),('callees',9)]:
   text=(a.source_dir/('wide-stack-'+part+'.c')).read_text()
   asm[part],_=compile_one(model+'-'+opt+'-'+part,text,model,opt)
   assert len(re.findall(r'wide-mir accepted:',asm[part]))==count
   assert 'wide-mir fallback' not in asm[part]
   assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm[part])
  assert 'wide stack call args=6 bytes=14' in asm['callers']
  # Outgoing stack parameters require a real call plus caller cleanup.
  for target in ['h3','h4','h8','oldh4','recur4']:
   assert '\tecall\t___m251r4_'+target in asm['callers']
   assert '\tejmp\t___m251r4_'+target not in asm['callers']
  frames=dict((n,int(v)) for n,v in re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm['callers']))
  assert frames['pressure']>0 and frames['high_pressure']>0 and frames['local_object']>=8
  h8=asm['callees'].split('wide-mir accepted: h8;',1)[1].split('wide-mir accepted:',1)[0]
  offsets=set(int(v) for v in re.findall(r'entry-offset=(-?\d+)',h8))
  assert offsets=={-3,-5,-9,-10,-12,-16},offsets
  rows.append(dict(model=model,opt=opt,nativeFunctions=27,frames=frames,mixedStackOffsets=sorted(offsets)))

header='typedef unsigned char u8;typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
nine=','.join('u32 '+n for n in 'abcdefghi')
negative={
 'ninth-caller':f'u32 g({nine}) ABI;u32 f(u32 a,u32 b) ABI {{return g(a,b,a,b,a,b,a,b,a);}}',
 'ninth-callee':f'u32 f({nine}) ABI {{return a+i;}}',
 'volatile-stack-parameter':'u32 f(u32 a,u32 b,volatile u32 c) ABI {return c+c+a;}',
 'addressed-stack-parameter':'u32 g(u32 __xdata *) ABI;u32 f(u32 a,u32 b,u32 c) ABI {return g(&c)+a;}',
}
for model in ['small','large']:
 for name,source in negative.items():
  pair=[]
  for wide in [False,True]:
   asm,obj=compile_one(model+'-'+name+'-'+str(wide),header+source,model,'speed',wide)
   if name=='addressed-stack-parameter' and wide:
    assert 'wide-mir accepted: f;' in asm and 'wide parameter address entry-offset=-6' in asm
    assert '\tecall\t___m251r4_g' in asm and '\tejmp\t___m251r4_g' not in asm
   else:assert 'wide-mir accepted:' not in asm,(model,name)
   pair.append(obj)
  if name=='addressed-stack-parameter':rows.append(dict(model=model,boundary=name,addressedStackFormalAccepted=True))
  else:
   assert pair[0]==pair[1],(model,name)
   rows.append(dict(model=model,boundary=name,fallbackMachineCodeIdentical=True))
 # The frontend evaluates these nested calls before pushing h's arguments.
 # This is a supported sequence, not evidence of an interleaved-IPUSH fallback.
 nested='u32 g(u32,u32) ABI;u32 h(u32,u32,u32,u32) ABI;u32 f(u32 a,u32 b) ABI {return h(a,b,g(a,b),g(b,a));}'
 asm,_=compile_one(model+'-nested-call',header+nested,model,'speed')
 assert 'wide-mir accepted: f;' in asm
 assert re.findall(r'ecall\s+___m251r4_(\w+)',asm)==['g','g','h']
 assert 'wide stack call args=2 bytes=8' in asm and '\tsub\tspx,#8' in asm
 rows.append(dict(model=model,nestedCallsEvaluatedBeforeStackPush=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2))
print('PASS 4 native configurations, 8 whole-function fallback boundaries, 2 nested-call configurations; pointer cases migrated to check-wide-stack-pointers.py')
