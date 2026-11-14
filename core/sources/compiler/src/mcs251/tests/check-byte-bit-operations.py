"""Check byte selection, bit predicates and ordered volatile transactions."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env.update(SDCC_HOME=str(a.toolchain.resolve()),MX251_WIDE_VALUE_LIMIT='1024',MX251_WIDE_NODE_LIMIT='2048')
env['PATH']=str(a.toolchain.resolve()/'bin')+os.pathsep+env['PATH']
tests=Path(__file__).parent

def functions(asm):
    return {m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
            r'; mx251-function end schema=1',asm,re.S)}

def transactions(body):
    accesses=[]
    for line in body.splitlines():
        match=re.match(r'\s*mov\s+([^,]+),([^;]+)',line)
        if not match:continue
        for index,operand in enumerate(match.groups()):
            address=re.fullmatch(r'e:\((_\w+)\+(\d+)\)',operand.strip())
            if address:accesses.append(('store' if index==0 else 'load',address[1],int(address[2])))
    return accesses

rows=[]
for model in ('small','large'):
 for opt in ('size','speed'):
  for abi in (2,4):
    root=a.output/f'{model}-{opt}-abi{abi}';root.mkdir()
    variants={}
    for variant,compiler in [('before',a.baseline),('after',a.sdcc)]:
        out=root/variant;out.mkdir()
        (out/'probe.c').write_bytes((tests/'byte-bit-operations.c').read_bytes())
        # Existing equivalent bitfield spellings also exercise composed masks.
        (out/'bits.c').write_bytes((tests/'user-stress/bits.c').read_bytes())
        (out/'stress.h').write_bytes((tests/'user-stress/stress.h').read_bytes())
        bodies={}
        for unit in ('probe','bits'):
            cmd=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                 '--std-sdcc11','--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir',
                 '--mcs251-function-metadata','-DTEST_ABI='+str(abi),'-c',unit+'.c']
            r=subprocess.run(cmd,cwd=out,env=env,capture_output=True)
            (out/(unit+'.log')).write_bytes(r.stdout+r.stderr)
            assert r.returncode==0,(out,unit,r.stderr)
            bodies.update(functions((out/(unit+'.asm')).read_text()))
        variants[variant]=bodies
    current=variants['after']
    for name,new in current.items():
        old=variants['before'][name]
        assert transactions(old)==transactions(new),(root,name,transactions(old),transactions(new))
    for name in ('_bits_clean','_bits_messy'):
        body=current[name]
        assert 'wide composed byte masks' in body,(root,name)
        assert 'wide byte rotate-mask count=7 right=0' in body,(root,name)
        assert re.search(r'\bj(?:n)?b\s+acc\.7,',body),(root,name)
        assert re.search(r'wide predicate branch .*mask=80 load=[1-9]',body),(root,name)
    for k in range(8):
        name=('_byte_' if abi==2 else '___m251r4_byte_')+str(k)
        assert 'wide-mir accepted: byte_'+str(k) in current[name],(root,name)
        assert len(transactions(current[name]))==5,(root,name,transactions(current[name]))
    rows.append(dict(model=model,optimization=opt,abi=abi,functions=len(current),
        volatileByteOrderPreserved=True,composedMasks=True,directHighBitBranch=True))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: eight configurations; byte shifts, composed masks, direct predicates, unchanged volatile transactions')
