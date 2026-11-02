#!/usr/bin/env python3
"""Typed binary32 transport/calls and conservative unsupported-operation fallback."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
negative={
 'negate':'float f(float a) ABI {return -a;}',
 'truth':'unsigned char f(float a) ABI {return !a;}',
 'condition':'float f(float a,float b) ABI {return a?b:1.0f;}',
 'comparison':'unsigned char f(float a,float b) ABI {return a<b;}',
 'to_integer':'unsigned long f(float a) ABI {return (unsigned long)a;}',
 'from_integer':'float f(unsigned long a) ABI {return (float)a;}',
 'volatile':'volatile float g;float f(float a) ABI {g=a;return g;}',
 'address_taken':'void sink(float *) __reentrant;float f(float a) ABI {sink(&a);return a;}',
 'legacy':'float g(float,float) __reentrant;float f(float a,float b) ABI {return g(a,b);}',
 'third_argument':'float g(float,float,float) ABI;float f(float a,float b) ABI {return g(a,b,a);}',
}
memory={
 'float_load':'float f(float __xdata *a) ABI {return *a;}',
 'float_store':'float f(float __xdata *a,float b) ABI {*a=b;return b;}',
}
for model in ['small','large']:
 flags=['-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-native-abi','--mcs251-native-runtime','--mcs251-native-int-runtime']
 def compile(name,source,wide):
  d=a.output_dir/(model+'-'+name);d.mkdir();(d/'core.c').write_text(source,encoding='utf8')
  args=[str(a.sdcc.resolve()),*flags,*(['--mcs251-wide-backend'] if wide else []),'-c','core.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(name,q.stderr)
  return d,args
 d,args=compile('positive',a.source.read_text(encoding='utf8'),True);asm=(d/'core.asm').read_text()
 accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
 assert len(accepted)==19 and 'wide-mir fallback:' not in asm
 assert re.search(r'wide-mir accepted: pressure;.*frame=[1-9]',asm)
 for name,bits in [('literal_pos',0x3fa00000),('literal_neg',0xc0200000),('literal_negzero',0x80000000),('literal_subnormal',1),('literal_max',0x7f7fffff)]:
  body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
  if bits<=0xffff:assert re.search(r'mov\s+dr\d+,#0x%04x'%bits,body),(name,body)
  else:
   for half in [bits>>16,bits&65535]:assert f'#0x{half:04x}' in body,(name,body)
 rows.append(dict(model=model,positiveFunctions=accepted,args=args,pressureSpill=True,literalBitsVerified=True))
 for name,src in memory.items():
  d,args=compile(name,'#define ABI __reentrant __sdcccall(4)\n'+src,True)
  asm=(d/'core.asm').read_text();assert 'wide-mir accepted: f;' in asm and 'wide-mir fallback:' not in asm,name
  assert 'width=4 volatile-bytes=0' in asm,name
  rows.append(dict(model=model,nativeFloatMemory=name,args=args))
 for name,src in negative.items():
  off,_=compile(name+'-off','#define ABI __reentrant __sdcccall(4)\n'+src,False)
  on,args=compile(name+'-on','#define ABI __reentrant __sdcccall(4)\n'+src,True)
  if name=='third_argument':
   assert 'wide-mir accepted: f;' in (on/'core.asm').read_text(),name
   assert 'wide stack call args=1 bytes=4' in (on/'core.asm').read_text()
   rows.append(dict(model=model,newStackFloatArgument=name,args=args,accepted=True))
  elif name=='address_taken':
   asm=(on/'core.asm').read_text()
   assert 'wide-mir accepted: f;' in asm and 'wide object a slot=0 size=4' in asm
   assert 'wide local snapshot store' in asm and 'wide local snapshot load' in asm and '\tecall\t_sink' in asm and '\tejmp\t_sink' not in asm
   rows.append(dict(model=model,addressedFloatFormalHome=True,args=args))
  elif name=='legacy':
   asm=(on/'core.asm').read_text()
   assert 'wide-mir accepted: f;' in asm and 'wide ordinary ABI call: args=2 stack=4' in asm
   assert '\tecall\t_g' in asm and '\tejmp\t_g' not in asm
   rows.append(dict(model=model,ordinaryFloatCallAccepted=True,args=args))
  elif name=='volatile' and model=='small':
   asm=(on/'core.asm').read_text()
   assert 'wide-mir accepted: f;' in asm and 'wide-mir fallback:' not in asm
   assert re.findall(r'wide symbol (store|load) _g width=4 volatile=1',asm)==['store','load']
   assert len(re.findall(r'mov\s+\(_g\+[0-3]\),(?:a|r\d+)',asm))==4
   assert len(re.findall(r'mov\s+(?:a|r\d+),\(_g\+[0-3]\)',asm))==4
   rows.append(dict(model=model,nativeVolatileGlobal=True,orderedByteAccesses=8,args=args))
  else:
   assert 'wide-mir accepted:' not in (on/'core.asm').read_text(),name
   assert (off/'core.rel').read_bytes()==(on/'core.rel').read_bytes(),name
   rows.append(dict(model=model,negative=name,args=args,fallbackObjectIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS float transport, literals, spills, memory, direct volatile global, fallback equivalence and stack-float callers; hardware not run by this script')
