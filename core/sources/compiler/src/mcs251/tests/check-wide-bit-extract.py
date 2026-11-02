"""Check selection transactions; actual peripherals are not exercised here."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args();a.output_dir.mkdir(exist_ok=False);rows=[]
header='typedef unsigned char u8;typedef unsigned long u32;\n#define ABI __reentrant __sdcccall(4)\n'
cases={
 'sfr':'__sbit __at(0x99) g;u32 f(u32 a,u32 b) ABI {g=a&0x100UL;g=b&0x80000000UL;return g;}',
 'ram':'extern volatile __bit g;u32 f(u32 a,u32 b) ABI {g=a&0x100UL;g=b&0x80000000UL;return g;}',
 'multiuse':'extern volatile __bit g;u32 f(u32 a,u32 b) ABI {u8 x=(__bit)(a&0x100UL);g=x;return a+x;}',
 'volatile':'volatile __data u32 g;u32 f(u32 a,u32 b) ABI {u32 x=g,y=g;return (u8)(__bit)(x&0x100UL)+(u8)(__bit)(y&0x80000000UL);}',
}
for model in ['small','large']:
 for opt in ['speed','size']:
  for name,src in cases.items():
   d=a.output_dir/(model+'-'+opt+'-'+name);d.mkdir();(d/'core.c').write_text(header+src)
   cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','core.c']
   q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
   asm=(d/'core.asm').read_text();assert 'wide-mir accepted: f;' in asm and 'wide getabit' in asm,(name,asm)
   if name in ['sfr','ram']:
    assert asm.count('wide getabit-store fused _g')==2 and len(re.findall(r'\bmov\s+_g,c\b',asm))==2
    assert len(re.findall(r'\bmov\s+c,_g\b',asm))==1
    assert not re.search(r'\bmov\s+(?:a|r\d+),_g\b|\b(?:setb|clr)\s+_g\b',asm)
   elif name=='multiuse':
    assert 'wide getabit-store fused' not in asm and re.search(r'mov\s+c,acc\.0\s+clr\s+a\s+rlc\s+a',asm)
   else:
    reads=re.findall(r'\bmov\s+(?:r\d+|a),\(_g\+(\d+)\)',asm)
    assert reads==['0','1','2','3']*2,(name,reads,asm)
   rows.append(dict(model=model,opt=opt,case=name,passed=True,peripheralHardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'bit extraction and access-count checks')
