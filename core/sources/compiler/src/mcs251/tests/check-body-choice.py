"""Check whole-body selection and symbol offsets against actual compilation."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'support', 'gcc', 'output'):
    p.add_argument('--'+key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
driver = a.output/'offset.c'
driver.write_text('#include <stdio.h>\n#include "backend_encoding.h"\nint main(int argc,char **argv) { for(int i=1;i<argc;++i)printf("%d\\n",bc_symbol_offset(argv[i],"_Object",32)); return 0; }\n')
exe = a.output/'offset.exe'
subprocess.run([str(a.gcc.resolve()), '-std=c99', '-O2', '-I'+str(tests.parent), str(driver), '-o', str(exe)], check=True)
cases = {'_Object':0, '((_Object+0x0006)+1)':7, '(_Object+(8-1))':7,
         '((_Object+9)-2)':7, '(((_Object)))':0, '0+_Object+31':31,
         '_Object+32':-1, '_Object-1':-1, '_Object+0x80000000':-1,
         '_Object+0xffffffff':-1, '_Object+4294967296':-1,
         '_Object+2147483647+1':-1, '_Object+2147483647+1-2147483647':-1,
         '_Object+_Object':-1, '_ObjectSuffix':-1, '_object':-1,
         '7-_Object':-1, '7-(_Object+1)':-1, '7-(1-_Object)':-1,
         '_Object+(1':-1, '(_Object+1))':-1, '_Object+':-1,
         '_Object*2':-1, '_Object/2':-1, '_Object+unknown':-1,
         '_Object+0x':-1, '((_Object+0x0006)+0x1)':7}
q = subprocess.run([str(exe), *cases], check=True, capture_output=True, text=True)
assert list(map(int, q.stdout.split())) == list(cases.values())
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small','large'):
    for opt in ('speed','size'):
        d = a.output/(model+'-'+opt); d.mkdir()
        for name in ('machine-board.h','body-choice-cases.c','body-choice-callee.c'):
            shutil.copy2(tests/name,d/name)
        cmd = [str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
               '--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir',
               '--mcs251-function-metadata','-c','body-choice-cases.c']
        q = subprocess.run(cmd,cwd=d,env=env,capture_output=True)
        (d/'compile.log').write_bytes(q.stdout+q.stderr)
        assert q.returncode==0,q.stderr
        text=(d/'body-choice-cases.asm').read_text()
        assert '; mx251-body-choice function=_apply_feature selected=legacy' in text
        assert '; mx251-body-choice function=_body_loop' not in text
        assert '; mx251-body-choice function=_body_goto' not in text
        assert (d/'body-choice-cases.rel').is_file()
        rows.append(dict(model=model,optimization=opt,command=cmd))
# The research A/B switch retains the preceding selection policy.
d = a.output/'disabled'; d.mkdir()
for name in ('machine-board.h','body-choice-cases.c'):shutil.copy2(tests/name,d/name)
env['MX251_BODY_BACKEND_CHOICE']='0'
q = subprocess.run(cmd,cwd=d,env=env,capture_output=True)
assert q.returncode==0,q.stderr
text=(d/'body-choice-cases.asm').read_text()
assert '; mx251-body-choice ' not in text
assert 'function=_apply_feature selected=wide' in text
report=dict(status='PASS',offsetCases=len(cases),configurations=rows,disabledPolicy='PASS',cycles='structured and irreducible excluded')
(a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',len(cases),'offset cases, 4 compiled/assembled configurations, structured/irreducible cycles, A/B policy')
