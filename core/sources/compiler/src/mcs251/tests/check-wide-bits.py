"""Validate actual bit transactions, constant byte writes and rejected public bit ABI."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args();a.output_dir.mkdir(exist_ok=False);rows=[]
header='typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
cases={
 'ram':('extern volatile __bit g;u32 f(u32 a,u32 b) ABI {g=a;if(g)g=b;return g;}',2),
 'sfr':('__sbit __at(0x99) g;u32 f(u32 a,u32 b) ABI {g=a;if(g)g=b;return g;}',2),
 'constants':('extern volatile __bit g;u32 f(u32 a,u32 b) ABI {g=0;g=1;return g;}',1),
 'constant_bytes':('volatile __data u32 g;u32 f(u32 a,u32 b) ABI {g=0x12345678UL;return g;}',0),
 'bit_parameter':('u32 f(__bit a) ABI {return a;}',None),
 'bit_return':('__bit f(u32 a) ABI {return a!=0;}',None),
 'bit_call':('__bit other(u32 a) ABI;u32 f(u32 a) ABI {return other(a);}',None),
}
for model in ['small','large']:
 for opt in ['speed','size']:
  for name,(source,reads) in cases.items():
   d=a.output_dir/(model+'-'+opt+'-'+name);d.mkdir();(d/'core.c').write_text(header+source)
   args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','core.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr)
   if reads is None:
    assert q.returncode and b'ABI' in q.stderr,(name,q.stderr)
    assert q.returncode==1 and not re.search(rb'Internal Error|SIGSEGV|Segmentation',q.stdout+q.stderr),(name,q.returncode,q.stderr)
    assert not (d/'core.rel').exists(),name
    rows.append(dict(model=model,opt=opt,case=name,rejectedWithAbiDiagnostic=True,cleanRejection=True));continue
   assert not q.returncode,(name,q.stderr)
   asm=(d/'core.asm').read_text();assert 'wide-mir accepted: f;' in asm,(name,asm)
   if reads:
    assert len(re.findall(r'\bmov\s+c,_g\b',asm))==reads,(name,asm)
    # SFR 位操作不能改为整字节读写；可执行分支上的 setb/clr 各一次。
    assert not re.search(r'\bmov\s+(?:a|r\d+),_g\b|\bmov\s+_g,',asm)
    stores=re.findall(r'\b(setb|clr)\s+_g\b',asm)
    assert len(stores)==(2 if name=='constants' else 4),(name,stores)
    if name=='constants':assert stores==['clr','setb']
    else:assert asm.count('wide bit cast-store fused')==2 and 'wide bit branch _g' in asm
   else:
    transactions=re.findall(r'\bmov\s+\(_g\+(\d+)\),#0x([0-9a-f]+)',asm)
    assert transactions==[('0','12'),('1','34'),('2','56'),('3','78')],transactions
   rows.append(dict(model=model,opt=opt,case=name,transactionsChecked=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'bit/SFR/constant-store/ABI checks; SFR inspected offline only')
