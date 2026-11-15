"""Compare complete candidates and execute their emitted 24-bit address sequence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','support','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support.resolve()),PATH=str(a.support.resolve()/'bin')+os.pathsep+env['PATH'])

def instructions(text):
    return [s for line in text.splitlines() if (s:=line.split(';',1)[0].strip())]

def bodies(text):
    return {m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=\S*_(si_\w+) .*?\n(.*?)'
        r'; mx251-function end schema=1',text,re.S)}

rows=[];sequences=set();widths=set()
for model in ('small','large'):
 for opt in ('speed','size'):
  for abi in (2,4):
   variants={};sizes={}
   for mode,compiler in [('before',a.baseline),('after',a.sdcc),('disabled',a.sdcc)]:
    d=a.output/f'{model}-{opt}-abi{abi}'/mode;d.mkdir(parents=True)
    for name in ('symbolic-index.h','symbolic-index-access.c'):shutil.copy2(tests/name,d/name)
    local=dict(env)
    if mode=='disabled':local['MX251_SYMBOLIC_INDEX']='0'
    command=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto',
        '--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata',
        '-DTEST_ABI='+str(abi),'-c','symbolic-index-access.c']
    q=subprocess.run(command,cwd=d,env=local,capture_output=True,timeout=120)
    (d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
    variants[mode]=(d/'symbolic-index-access.asm').read_text()
    sizes[mode]=sum(int(v,16) for v in re.findall(r'^A CSEG(?:\$\S+)? size ([0-9A-Fa-f]+)',
        (d/'symbolic-index-access.rel').read_text(),re.M))
   assert instructions(variants['before'])==instructions(variants['disabled']),(model,opt,abi,'disabled differs')
   assert sizes['after']<=sizes['before'],(model,opt,abi,sizes)
   old,new=bodies(variants['before']),bodies(variants['after'])
   assert len(new)==8,new.keys()
   for name in ('si_negative','si_dynamic','si_ordered','si_branch'):
    assert instructions(old[name])==instructions(new[name]),(model,opt,abi,name)
   selected=re.findall(r'^; wide symbolic-index choice function=(\w+).*? selected=1 .*?old-bytes=(\d+) new-bytes=(\d+)',variants['after'],re.M)
   assert selected,(model,opt,abi,'no selected function')
   for name,before,after in selected:assert int(after)<int(before),(name,before,after)
   widths.update(map(int,re.findall(r'^; wide symbolic-index folded .*? width=(\d+) ',variants['after'],re.M)))
   # These are the actual emitted instructions, with their relocatable symbol.
   sequences.update(re.findall(r'^; wide symbolic-index address[^\n]*\n((?:\t[^\n]*\n){4})',variants['after'],re.M))
   rows.append(dict(model=model,optimization=opt,abi=abi,bytes=sizes,selected=selected))
assert widths=={1,2,4},widths

# Execute ADD/MOV/ADDC/MOVZ with independent register-width and CY semantics.
# Use every word index around low-word carry and 24-bit wrapping boundaries.
# This is an address model, not a claim of whole-firmware hardware execution.
checks=0
for sequence in sorted(sequences):
 for base in (0,0xff80,0x10000,0x12fff8,0xff8000,0xfffff8):
  for index in range(65536):
   regs={'wr18':index,'wr16':0xbeef,'a':0x55};carry=0;symbol_offset=None
   for line in sequence.splitlines():
    op,operands=line.strip().split(None,1);dest,source=operands.split(',')
    if source.startswith('#'):
     value=source[1:]
     m=re.search(r'(_\w+)\+(\d+)',value)
     if m:
      symbol_offset=int(m[2]);value=value.replace(m[0],str(base+symbol_offset))
     assert re.fullmatch(r'[0-9()+>]+',value),value
     rhs=eval(value,{'__builtins__':{}},{})
    else:rhs=regs[source]
    bits=8 if dest=='a' else 16;mask=(1<<bits)-1
    if op in ('add','addc'):
     total=regs[dest]+(rhs&mask)+(carry if op=='addc' else 0)
     regs[dest]=total&mask;carry=int(total>mask)
    elif op in ('mov','movz'):regs[dest]=rhs&mask
    else:raise AssertionError(op)
   actual=(regs['wr16']<<16)|regs['wr18']
   assert actual==(base+symbol_offset+index)&0xffffff,(sequence,base,index,actual)
   checks+=1
result=dict(status='PASS',configurations=rows,addressModelChecks=checks,accessWidths=sorted(widths),
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
    baselineSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest(),hardwareExecuted=False)
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: 8 modes, unchanged controls, 1/2/4-byte access candidates,',checks,'address-model cases')
