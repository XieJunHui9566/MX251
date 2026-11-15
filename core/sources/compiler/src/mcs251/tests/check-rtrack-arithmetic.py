"""Check real register tracking and the decimal-loop compiler crash."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('gcc', 'generated', 'sdcc', 'toolchain', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
compiler = tests.parents[2]
includes = [a.generated, a.generated/'mcs251', compiler/'src', compiler,
            compiler/'support/util', tests.parent]
command = [str(a.gcc), '-std=gnu17', '-O2', *['-I'+str(d) for d in includes],
           str(tests/'rtrack-arithmetic-host.c'), '-o', str(a.output/'tracker.exe')]
q = subprocess.run(command, capture_output=True)
(a.output/'host-build.log').write_bytes(q.stdout+q.stderr)
assert q.returncode == 0, q.stderr
q = subprocess.run([str((a.output/'tracker.exe').resolve())], capture_output=True)
(a.output/'host-run.log').write_bytes(q.stdout+q.stderr)
assert q.returncode == 0, q.stderr
cases = int(q.stdout)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env.update(SDCC_HOME=str(a.toolchain.resolve()), PATH=str(a.toolchain.resolve()/'bin')+os.pathsep+env['PATH'])
rows = []
for model in ('small', 'large'):
    for abi in ('ordinary', 'native'):
        root = a.output/(model+'-'+abi)
        root.mkdir()
        shutil.copy2(tests/'rtrack-decimal.c', root/'rtrack-decimal.c')
        command = [str(a.sdcc.resolve()), '-mmcs251', '--model-'+model,
                   '--opt-code-size', '--stack-auto', '--mcs251-native-abi',
                   '--mcs251-wide-ordinary', '--mcs251-machine-ir', '-c', 'rtrack-decimal.c']
        if abi == 'native':
            command += ['-DDECIMAL_ABI=__reentrant __sdcccall(4)']
        q = subprocess.run(command, cwd=root, env=env, capture_output=True)
        (root/'compile.log').write_bytes(q.stdout+q.stderr)
        assert q.returncode == 0, (root, q.returncode, q.stderr)
        asm = (root/'rtrack-decimal.asm').read_text()
        assert 'wide unsigned word division' in asm and (root/'rtrack-decimal.rel').is_file()
        rows.append(dict(model=model, abi=abi, status='PASS'))
result = dict(status='PASS', trackerCases=cases, targetCompiles=rows, hardwareExecuted=False)
(a.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result))
