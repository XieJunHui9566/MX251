"""Compile stack constants and local addresses across both models and modes."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
rows, commands = [], []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('machine-board.h', 'value-storage.h', 'value-storage.c', 'value-storage-sinks.c'):
            shutil.copy2(tests / name, d / name)
        # The board case includes byte remainder, which uses the legacy path.
        # Use a bounded mask here to exercise volatile stores in wide MIR too.
        source = d / 'value-storage.c'
        source.write_text(source.read_text().replace('seed%5', 'seed&3'))
        for name in ('value-storage', 'value-storage-sinks'):
            command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
                       '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
                       '--mcs251-function-metadata', '-c', name + '.c']
            q = subprocess.run(command, cwd=d, env=env, capture_output=True)
            (d / (name + '.log')).write_bytes(q.stdout + q.stderr)
            commands.append(dict(command=command, directory=str(d), exitCode=q.returncode))
            assert q.returncode == 0, (command, q.stdout, q.stderr)
        asm = (d / 'value-storage.asm').read_text()
        assert re.search(r'^; wide stack-call-address choice function=vs_buffers .*selected=1 ', asm, re.M)
        assert re.search(r'^; wide private-values choice function=vs_values .*selected=1 ', asm, re.M)
        assert '; wide stack constants object=' in asm
        volatile = asm.split('symbol=_vs_volatile ', 1)[1].split('; mx251-function end', 1)[0]
        assert 'wide-mir accepted: vs_volatile;' in volatile
        assert not re.search(r'^; wide stack constants ', volatile, re.M)
        assert volatile.count('; wide memory store width=1 volatile-bytes=1') >= 5
        assert not re.search(r'\br(?:1[6-9]|[23]\d)\b', asm)
        rows.append(dict(model=model, optimization=opt, stackCallAddresses=True,
                         privateValues=True, literalWords=True, volatileBytesRetained=True))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows, commands=commands), indent=2)+'\n')
print('PASS: small/large x speed/size; private values, literal words, recursive/variadic addresses, volatile bytes')
