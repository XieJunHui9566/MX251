"""ABI2 user-call transport and deliberately unsupported call boundaries."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False);T=Path(__file__).resolve().parent;rows=[]
prefix='typedef unsigned char u8;typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
negative={
 'abi0':'u32 g(u32) __reentrant __sdcccall(0);u32 f(u32 a) ABI {return g(a);}',
 'abi1':'u32 g(u32) __reentrant __sdcccall(1);u32 f(u32 a) ABI {return g(a);}',
 'nine':'u32 g(u32,u32,u32,u32,u32,u32,u32,u32,u32) __reentrant;u32 f(u32 a,u32 b) ABI {return g(a,b,a,b,a,b,a,b,a);}',
 'critical':'u32 g(u32) __reentrant __critical;u32 f(u32 a) ABI {return g(a);}',
 'naked':'u32 g(u32) __reentrant __naked;u32 f(u32 a) ABI {return g(a);}',
 'bank':'u32 g(u32) __reentrant __using(1);u32 f(u32 a) ABI {return g(a);}',
 'indirect':'typedef u32 (*F)(u32) __reentrant;F g;u32 f(u32 a) ABI {return g(a);}',
 'variadic':'u32 g(u32,...) __reentrant;u32 f(u32 a) ABI {return g(a,a);}',
 'codeptr':'u32 g(const u8 __code *) __reentrant;u32 f(u32 a) ABI {return g((const u8 __code *)a);}',
 'formal':'u32 g(u32 *) __reentrant;u32 f(u32 a) ABI {return g(&a);}',
 'runtime':'u32 f(float a) ABI {return (u32)a;}',
}
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir()
  for name in ['wide-ordinary.h','wide-ordinary-callers.c']: (d/name).write_bytes((T/name).read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','wide-ordinary-callers.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0
  asm=(d/'wide-ordinary-callers.asm').read_text()
  accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==11 and re.findall(r'wide-mir fallback: (\w+)',asm)==['c_float']
  assert asm.count('wide ordinary ABI call:')==10
  assert 'wide ordinary ABI call: args=8 stack=17' in asm
  for name in ['float_bridge','c_dword']:
   body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; wide allocation selected=',1)[0]
   assert '\tecall\t' in body and '\tejmp\t_float_value' not in body and '\tejmp\t_dword_value' not in body
  assert 'movz\twr16,r10' in asm and 'xch\ta,b' in asm
  pressure=asm.split('wide-mir accepted: c_pressure;',1)[1]
  assert int(re.search(r'frame=(\d+)',pressure)[1])>1 and 'reg=-1' in pressure and '@spx' in pressure
  rows.append(dict(model=model,opt=opt,accepted=accepted,ordinaryCalls=10,stackSevenArgumentsBytes=17,spills=True))
 for name,src in negative.items():
  objects=[]
  for enabled in [False,True]:
   d=a.output_dir/(model+'-'+name+'-'+str(enabled));d.mkdir();(d/'test.c').write_text(prefix+src)
   args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi']+(['--mcs251-wide-backend'] if enabled else [])+['-c','test.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,(name,q.stderr)
   asm=(d/'test.asm').read_text()
   if name=='formal' and enabled:
    assert 'wide-mir accepted: f;' in asm and 'wide object a slot=0 size=4' in asm
    assert '\tecall\t_g' in asm and '\tejmp\t_g' not in asm
   else:assert 'wide-mir accepted:' not in asm,name
   objects.append((d/'test.rel').read_bytes())
  if name=='formal':rows.append(dict(model=model,boundary=name,addressedFormalAccepted=True))
  else:
   assert objects[0]==objects[1],name
   rows.append(dict(model=model,boundary=name,fallbackObjectIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ordinary-call configurations/boundaries')
