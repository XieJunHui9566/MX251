"""Check flat data-pointer provenance and unchanged unknown-address paths."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','baseline','toolchain','output'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env.update(SDCC_HOME=str(a.toolchain.resolve()),MX251_WIDE_VALUE_LIMIT='1024',MX251_WIDE_NODE_LIMIT='2048')
env['PATH']=str(a.toolchain.resolve()/'bin')+os.pathsep+env['PATH']
source=Path(__file__).with_name('fixed-pointer-addresses.c').read_bytes()
rows=[]

def functions(asm):
    result={}
    for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
                         r'; mx251-function end schema=1',asm,re.S):
        result[m[1]]=m[2]
    return result

def code(body):
    # Legacy register equates are emitted once per translation unit; an earlier
    # function selecting MIR can move those declarations to the next function.
    return '\n'.join(line for raw in body.splitlines() if (line:=raw.split(';')[0].strip())
                     and not re.match(r'ar[0-7]\s*=',line))

for model in ('small','large'):
    for opt in ('size','speed'):
        for abi in (2,4):
            root=a.output/f'{model}-{opt}-abi{abi}';root.mkdir()
            variants={}
            for variant,compiler in [('before',a.baseline),('after',a.sdcc)]:
                out=root/variant;out.mkdir();(out/'probe.c').write_bytes(source)
                cmd=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                     '--std-sdcc11','--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir',
                     '--mcs251-function-metadata','-DTEST_ABI='+str(abi),'-c','probe.c']
                r=subprocess.run(cmd,cwd=out,env=env,capture_output=True)
                (out/'compile.log').write_bytes(r.stdout+r.stderr)
                assert r.returncode==0,(out,r.stderr)
                variants[variant]=functions((out/'probe.asm').read_text())
            folded={}
            for name,new in variants['after'].items():
                old=variants['before'][name]
                if 'negative_' in name:
                    assert code(old)==code(new),(root,name,'unknown-address path changed')
                else:
                    assert 'wide-mir accepted: positive_' in new,(root,name)
                    folded[name]=len(re.findall(r'^; wide (?:edata|stack)-address folded ',new,re.M))
                    if name.endswith('_positive_stack'):
                        assert new.count('; wide stack direct object=')==4,(root,name)
                    else:
                        assert re.search(r'wide object-address choice .* selected=1 ',new),(root,name)
                        assert folded[name]>=3,(root,name,folded)
                    if name.endswith('_positive_generic'):
                        # Verify actual emitted byte transactions, not just MIR annotations.
                        accesses=[]
                        for line in code(new).splitlines():
                            match=re.search(r'\bmov\s+([^,]+),(.+)',line)
                            if not match:continue
                            for index,operand in enumerate(match.groups()):
                                address=re.fullmatch(r'e:\(_cells\+(\d+)\)',operand.strip())
                                if address:accesses.append(('store' if index==0 else 'load',int(address[1])))
                        assert accesses==[('store',1),('load',1),('load',3)],(root,accesses)
            rows.append(dict(model=model,optimization=opt,abi=abi,folded=folded,
                             unchangedNegativeFunctions=5,volatileByteOrderPreserved=True))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: eight configurations; data/stack casts, five unchanged address boundaries, volatile byte order')
