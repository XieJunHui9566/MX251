"""Admission and physical access order for narrow products and discarded reads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
source = '''typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(TEST_ABI)
volatile __data u16 cell;
volatile __edata u32 extended;
volatile __xdata u32 farcell;
static volatile __xdata u32 initialized=0x98765432UL;
const volatile __code u16 rom=0;
volatile __bit flag;
__sfr __at(0xb5) port;
u16 product(s8 a,s8 b) ABI {return a*b;}
u8 low_product(s8 a,s8 b) ABI {return a*b;}
u16 mixed_product(s8 a,u8 b) ABI {return a*b;}
u8 digit(u8 c) ABI;
u8 decimal(u8 *p,u8 count) ABI {u8 i,v=0;for(i=0;i<count;i++){
if(digit(p[i])>=10)break;v=v*10+digit(p[i]);}return v;}
u32 discarded(u32 x) ABI {port;cell;cell;extended;farcell;initialized;rom;flag;return x>>13;}
u32 paired(u32 x) ABI {cell^cell;return x>>13;}
u32 local_read(u32 x) ABI {volatile u16 local=25;local;return (x>>13)+local;}
u32 overwrite(u32 x) ABI {port|=255;port&=0;return x>>13;}
'''
rows=[]
for abi in (2,4):
    for model in ('small','large'):
        for opt in ('size','speed'):
            d=a.output/f'abi{abi}-{model}-{opt}';d.mkdir()
            (d/'fixture.c').write_text(source)
            # Deliberately omit the inline assembly capability flags.
            command=[str(a.sdcc),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                     '--stack-auto','--std-sdcc11','--mcs251-native-abi','--mcs251-wide-ordinary',
                     '--mcs251-function-metadata','-DTEST_ABI='+str(abi),'-c','fixture.c']
            result=subprocess.run(command,cwd=d,env=env,capture_output=True,text=True)
            (d/'compile.log').write_text(result.stdout+result.stderr)
            assert result.returncode==0,(d,result.stderr)
            asm=(d/'fixture.asm').read_text()
            assert 'wide refusal' not in asm,d
            for name in ('product','low_product','mixed_product','decimal','discarded','paired','local_read','overwrite'):
                assert 'wide-mir accepted: '+name+';' in asm,(d,name)
            body=asm.split('_discarded:\n',1)[1].split('; mx251-function end',1)[0]
            # Direct and EDATA transactions retain the legacy low-byte-first order.
            accesses=re.findall(r'\bmov\s+a,(?:e:)?\((_(?:cell|extended|port))\+(\d+)\)',body)
            assert accesses==[('_port','0'),('_cell','1'),('_cell','0'),('_cell','1'),('_cell','0')]+[
                ('_extended',str(k)) for k in (3,2,1,0)],(d,accesses)
            assert len(re.findall(r'\bmov\s+a,@dr16\b',body))==8,d
            assert len(re.findall(r'\bmovc\s+a,@a\+dptr',body))==2,d
            assert body.index('#(_rom+1)')<body.index('#(_rom+0)'),d
            assert len(re.findall(r'\bmov\s+c,_flag\b',body))==1,d
            body=asm.split('_paired:\n',1)[1].split('; mx251-function end',1)[0]
            assert re.findall(r'\bmov\s+a,\(_cell\+(\d+)\)',body)==['1','0','1','0'],(d,body)
            body=asm.split('_overwrite:\n',1)[1].split('; mx251-function end',1)[0]
            read=[m.start() for m in re.finditer(r'\bmov\s+a,\(_port\+0\)',body)]
            writes=[m.start() for m in re.finditer(r'\bmov\s+\(_port\+0\),#0x(?:ff|00)',body)]
            assert len(read)==len(writes)==2 and read[0]<writes[0]<read[1]<writes[1],(d,body)
            # Without --stack-auto the immutable register-parameter home is
            # removed before allocation. The discarded local must follow its
            # new object index and frame offset.
            compact=d/'compact';compact.mkdir();(compact/'fixture.c').write_text(source)
            result=subprocess.run([x for x in command if x!='--stack-auto'],cwd=compact,env=env,capture_output=True,text=True)
            (compact/'compile.log').write_text(result.stdout+result.stderr)
            assert result.returncode==0,(compact,result.stderr)
            text=(compact/'fixture.asm').read_text()
            body=text.split('wide-mir accepted: local_read;',1)[1].split('; mx251-function end',1)[0]
            assert 'wide object local slot=0 size=2' in body and 'wide object x ' not in body,compact
            assert re.search(r'wide volatile-read[^\n]*\n\s*mov\s+a,@spx\n\s*mov\s+a,@spx-0x0001',body),compact
            rows.append(dict(abi=abi,model=model,optimization=opt,functions=8,physicalAccessOrder=True,compactedFrame=True))
(a.output/'results.json').write_text(json.dumps(dict(status='PASS',rows=rows,
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest()),indent=2)+'\n')
print('PASS',len(rows),'configurations; narrow products and physical volatile transactions')
