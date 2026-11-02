#!/usr/bin/env python3
"""Direct objects are transactions, not cached virtual registers; unsupported spaces fall back."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False,parents=True);rows=[]
header='#define ABI __reentrant __sdcccall(4)\ntypedef unsigned long u32;\n'
positive={
 'volatile':'volatile __data u32 g;u32 f(u32 a,u32 b) ABI {g=a;g=b;return g+g;}',
 'static':'u32 f(u32 a,u32 b) ABI {static volatile __data u32 g;g=a;g+=b;return g;}',
 'sfr':'__sfr __at(0x95) g;unsigned char f(unsigned char a) ABI {g=a;return g;}',
 'signed_byte':'volatile __data signed char g;u32 f(u32 a) ABI {g=(signed char)a;return (signed long)g;}',
 'signed_word':'volatile __data signed int g;u32 f(u32 a) ABI {g=(signed int)a;return (signed long)g;}',
}
negative={
 'xdata':'volatile __xdata u32 g;u32 f(u32 a) ABI {g=a;return g;}',
 'idata':'volatile __idata u32 g;u32 f(u32 a) ABI {g=a;return g;}',
 'pdata':'volatile __pdata u32 g;u32 f(u32 a) ABI {g=a;return g;}',
 'bool':'_Bool g;u32 f(u32 a) ABI {g=(_Bool)a;return g;}',
 'sfr16':'__sfr16 __at(0x8c8a) g;u32 f(u32 a) ABI {g=(unsigned int)a;return g;}',
 'pointer':'u32 __xdata * __data g;u32 f(u32 a) ABI {return *g+a;}',
 'atomic':'volatile _Atomic u32 __data g;u32 f(u32 a) ABI {g=a;return g;}',
 'unsampled_argument':'volatile __data u32 g;u32 other(u32,u32) ABI;u32 f(u32 a) ABI {return other(g,a);}',
}
for model in ['small','large']:
 for opt in ['speed','size']:
  flags=['-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi','--opt-code-'+opt]
  def compile(name,source,enabled):
   d=a.output_dir/(model+'-'+opt+'-'+name+('-on' if enabled else '-off'));d.mkdir();(d/'core.c').write_text(header+source)
   args=[str(a.sdcc.resolve()),*flags,*(['--mcs251-wide-backend'] if enabled else []),'-c','core.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr)
   if name=='atomic':
    assert q.returncode and b'atomics are not supported' in q.stderr
    return d,None
   assert not q.returncode,(d,q.stderr)
   return d,(d/'core.asm').read_text()
  for name,source in positive.items():
   d,asm=compile(name,source,True);assert 'wide-mir accepted: f;' in asm and 'wide-mir fallback' not in asm,(d,asm)
   accesses=re.findall(r'wide symbol (store|load) (\w+) width=(\d) volatile=(\d)',asm)
   expected=['store','store','load','load'] if name=='volatile' else ['store','load','store','load'] if name=='static' else ['store','load']
   assert [x[0] for x in accesses]==expected,(d,accesses)
   assert all(x[3]=='1' for x in accesses)
   # 验证真正发射的每个字节事务，而不只验证诊断注释。
   chunks=re.split(r'; wide symbol ',asm)[1:]
   for (direction,symbol,width,volatile),chunk in zip(accesses,chunks):
    width=int(width)
    pattern=r'mov\s+\('+re.escape(symbol)+r'\+(\d+)\),(?:a|r\d+)' if direction=='store' else r'mov\s+(?:a|r\d+),\('+re.escape(symbol)+r'\+(\d+)\)'
    assert list(map(int,re.findall(pattern,chunk)))==list(range(width)),(d,direction,chunk)
   rows.append(dict(model=model,opt=opt,case=name,native=True,accesses=accesses))
  for name,source in negative.items():
   d,asm=compile(name,source,True);off,_=compile(name,source,False)
   if name=='atomic':
    rows.append(dict(model=model,opt=opt,case=name,rejectedByFrontend=True));continue
   assert 'wide-mir accepted' not in asm,(d,asm)
   assert (d/'core.rel').read_bytes()==(off/'core.rel').read_bytes(),d
   rows.append(dict(model=model,opt=opt,case=name,fallbackIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'global transaction and conservative fallback checks; SFR checked offline only')
