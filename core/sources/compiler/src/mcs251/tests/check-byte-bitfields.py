"""Check bitfield byte transactions, native ABI, CODE reads and wide-field fallback."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env['SDCC_HOME']=str(a.support.resolve());env['PATH']=str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
source='''
#define ABI __reentrant __sdcccall(TEST_ABI)
typedef unsigned char u8;
typedef unsigned long u32;
struct Fields { unsigned char keep:3; signed char value:5; };
struct Byte { unsigned char value:8; };
u32 sequence(volatile struct Fields *p, u32 input) ABI {
    p->value=input; return (signed long)p->value;
}
u32 full_byte(volatile struct Byte *p, u32 input) ABI {
    p->value=input; return p->value;
}
u32 code_read(const struct Fields __code *p) ABI {return (signed long)p->value;}
'''
negative='''
#define ABI __reentrant __sdcccall(TEST_ABI)
struct Wide { unsigned int value:12; unsigned int keep:4; };
unsigned long wide_field(volatile struct Wide *p,unsigned long input) ABI {
    p->value=input;return p->value;
}
'''
rows=[]
for abi in (2,4):
 for model in ('small','large'):
  for opt in ('size','speed'):
   d=a.output/f'abi{abi}-{model}-{opt}';d.mkdir()
   flags=['-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto','--mcs251-native-abi',
          '--mcs251-flat-code-reads','--mcs251-machine-ir','--mcs251-function-metadata','-DTEST_ABI='+str(abi)]
   def compile(name,text,wide):
    (d/(name+'.c')).write_text(text)
    command=[str(a.sdcc.resolve()),*flags,*(['--mcs251-wide-ordinary'] if wide else []),'-c',name+'.c']
    run=subprocess.run(command,cwd=d,env=env,capture_output=True)
    (d/(name+'.log')).write_bytes(run.stdout+run.stderr)
    assert run.returncode==0,(d,name,run.stderr)
    return (d/(name+'.asm')).read_text()
   asm=compile('probe',source,True)
   assert not re.findall(r'; wide refusal function=(\S+)',asm),(d,'unexpected fallback')
   transactions={}
   for name,expected in [('sequence',['load','store','load']),('full_byte',['store','load'])]:
    body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
    # Check actual byte MOVs, including direct register transport.
    ordered=re.findall(r'^\s*mov\s+((?:a|r\d+),@dr16|@dr16,(?:a|r\d+))\s*$',body,re.M)
    actual=['store' if x.startswith('@') else 'load' for x in ordered]
    assert actual==expected,(d,name,actual,expected)
    assert not re.search(r'\bmov\s+(?:wr\d+,@dr16|@dr16,wr\d+)',body),(d,name,'wide access')
    transactions[name]=actual
   compile('fallback',negative,True)
   fallback=(d/'fallback.rel').read_bytes()
   # Keep source and module name identical when comparing object contents.
   compile('fallback',negative,False)
   assert fallback==(d/'fallback.rel').read_bytes(),(d,'wide-field fallback changed')
   rows.append(dict(abi=abi,model=model,optimization=opt,transactions=transactions,
                    codeReadAccepted=True,wideFieldObjectIdentical=True))
(a.output/'results.json').write_text(json.dumps(rows,indent=2))
print('PASS: 8 configurations; ordered byte transactions, CODE reads, identical wide-field fallback')
