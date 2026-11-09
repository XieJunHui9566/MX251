"""Run the production copy-flow pass and compile its MCU call-pressure fixture."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'cc', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
commands = []


def run(command, cwd, log, env=None):
    command = [str(x) for x in command]
    q = subprocess.run(command, cwd=cwd, env=env, capture_output=True)
    log.write_bytes(q.stdout + q.stderr)
    commands.append(dict(command=command, directory=str(cwd), exitCode=q.returncode))
    assert q.returncode == 0, (command, q.stderr, q.stdout)


exe = a.output.resolve() / 'value-copy-flow.exe'
run([a.cc.resolve(), '-std=c99', '-O2', '-Wall', '-Wextra', tests / 'value-copy-flow.c', '-o', exe],
    a.output, a.output / 'host-build.log')
run([exe], a.output, a.output / 'host-run.log')
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('machine-board.h', 'value-copy.h', 'value-copy-access.c', 'value-copy-sinks.c'):
            shutil.copy2(tests / name, d / name)
        for source in ('value-copy-access', 'value-copy-sinks'):
            run([a.sdcc.resolve(), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
                 '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
                 '--mcs251-function-metadata', '-c', source + '.c'], d, d / (source + '.log'), env)
        asm = (d / 'value-copy-access.asm').read_text()
        for name in ('fanout', 'mutating', 'snapshot', 'pointer'):
            assert 'wide-mir accepted: copy_' + name + ';' in asm, name
        assert re.search(r'^; wide value-copy choice function=copy_fanout .*selected=1 ', asm, re.M)
        sample = asm.split('symbol=_copy_snapshot ', 1)[1].split('; mx251-function end', 1)[0]
        assert sample.count('wide memory load width=2 volatile-bytes=1') == 2
        assert sample.count('wide memory store width=2 volatile-bytes=1') == 1
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=4, commands=commands), indent=2) + '\n')
print('PASS: CFG flow graphs; small/large x speed/size; fanout, source mutation, pointers, volatile transactions')
