"""CODE remains a distinct read-only address space; ABI and fallback stay explicit."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-int-runtime','--mcs251-wide-backend','-c','test.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(d/'test.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==12 and 'wide-mir fallback' not in asm
  accesses=re.findall(r'wide code load width=(\d) volatile-bytes=(\d)',asm);assert {int(w) for w,v in accesses}=={1,2,4}
  assert sum(int(w) for w,v in accesses)==asm.count('movc\ta,@a+dptr')
  volatile=asm.split('wide-mir accepted: c_volatile;',1)[1].split('; wide allocation ',1)[0]
  assert volatile.count('wide code load width=4 volatile-bytes=1')==2 and volatile.count('movc\ta,@a+dptr')==8
  assert '\tinc\tdptr' not in asm and '\tinc\tdpx' in asm
  first=(d/'test.rel').read_bytes();q=subprocess.run(args,cwd=d,capture_output=True);assert not q.returncode and first==(d/'test.rel').read_bytes()
  rows.append(dict(model=model,opt=opt,accepted=accepted,codeLoadSites=len(accesses),movcBytes=sum(int(w) for w,v in accesses),volatileBytes=8,deterministic=True))
header='typedef unsigned char u8;typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
negative={
 'code-to-data':'extern const __code u8 tab[520];u32 sink(const u8 *) ABI;u32 f(u32 n) ABI {return sink((const u8 *)(tab+(n&511)));}',
 'data-to-code':'u32 f(const u8 *p,u32 n) ABI {return ((const u8 __code *)p)[n&511];}',
 'pointer-in-memory':'extern const u8 __code * __xdata p;u32 f(u32 n) ABI {return p[n&511];}',
 'code-param':'u32 f(const u8 __code *p) ABI {return *p;}',
 'code-return':'const u8 __code *f(u32 n) ABI {return (const u8 __code *)n;}',
 'code-call':'u32 g(const u8 __code *) ABI;u32 f(u32 n) ABI {return g((const u8 __code *)n);}',
 'code-store':'void f(u32 n,u32 v) ABI {*(u8 __code *)n=v;}',
 'atomic-code':'u32 f(u32 n) ABI {return *(_Atomic(u32) __code *)n;}',
}
for model in ['small','large']:
 for name,source in negative.items():
  outputs=[]
  for wide in [False,True]:
   d=a.output_dir/(model+'-'+name+'-'+str(wide));d.mkdir();(d/'test.c').write_text(header+source)
   args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-int-runtime','--mcs251-native-abi']+(['--mcs251-wide-backend'] if wide else [])+['-c','test.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr)
   if not q.returncode:assert 'wide-mir accepted' not in (d/'test.asm').read_text()
   outputs.append((q.returncode,(d/'test.rel').read_bytes() if not q.returncode else q.stderr))
  assert (outputs[0][0]==0)==(outputs[1][0]==0)
  if not outputs[0][0]:assert outputs[0][1]==outputs[1][1],(model,name)
  rows.append(dict(model=model,boundary=name,diagnostic=bool(outputs[0][0]),fallbackMachineCodeIdentical=not outputs[0][0]))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 4 CODE configurations and 16 address-space/ABI boundaries')
