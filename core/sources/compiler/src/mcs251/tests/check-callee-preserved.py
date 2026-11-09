"""Compile both ABIs with explicit preservation and physical stack arguments."""
import argparse,json,re,shutil,subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--sdcc',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent;rows=[]
extra='''#include "tail-boundary.h"
u32 recursive(u32 x,u16 depth) NATIVE {
    volatile u16 guard[3];
    guard[0]=(u16)x; guard[2]=depth;
    if(!depth)return x^guard[0];
    return recursive(x+0x12345UL,depth-1)^guard[0]^guard[2];
}
'''
for model in ('small','large'):
    for opt in ('speed','size'):
        d=a.output_dir/(model+'-'+opt);d.mkdir()
        for name in ('tail-boundary.h','tail-boundary-callers.c','tail-boundary-callees.c'):
            shutil.copy2(tests/name,d/name)
        (d/'recursive.c').write_text(extra)
        commands=[]
        for name in ('tail-boundary-callers','tail-boundary-callees','recursive'):
            source=(d/(name+'.c')).read_text()
            symbols=[('___m251r4_' if abi=='NATIVE' else '_')+fn
                     for fn,abi in re.findall(r'\b(\w+)\([^\n]*?\) (OLD|NATIVE)\s*\{',source)]
            assert symbols,name
            contract='MX251-CALLEE-PRESERVE-1\n'+''.join(
                f'global {s} {4 if s.startswith("___m251r4_") else 2} ff00f300\n' for s in symbols)
            (d/(name+'.preserve')).write_text(contract)
            cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,
                 '--mcs251-wide-ordinary','--mcs251-function-metadata','--mcs251-machine-ir',
                 '--mcs251-callee-preserve',name+'.preserve','-c',name+'.c']
            q=subprocess.run(cmd,cwd=d,capture_output=True)
            (d/(name+'.log')).write_bytes(q.stdout+q.stderr)
            assert not q.returncode,q.stderr
            asm=(d/(name+'.asm')).read_text()
            for fn in ('_outgoing_stack','_parameter_address','_local_address','___m251r4_native_tail','___m251r4_recursive'):
                if fn not in symbols:continue
                m=re.search(r'^'+re.escape(fn)+r':\s*$(.*?)^; mx251-function end',asm,re.M|re.S)
                assert m and '; mx251-callee-preserved schema=1 saved=ff00f300' in m[1],fn
                assert re.search(r'push\s+wr8',m[1]) and re.search(r'pop\s+wr8',m[1]),fn
                for reg in (12,24,28):
                    assert re.search(r'push\s+dr'+str(reg),m[1]) and re.search(r'pop\s+dr'+str(reg),m[1]),fn
            commands.append(cmd)
        rows.append(dict(model=model,optimization=opt,commands=commands))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS: preservation, stack objects, addressed parameters, recursion and ABI2/ABI4 in 4 configurations')
