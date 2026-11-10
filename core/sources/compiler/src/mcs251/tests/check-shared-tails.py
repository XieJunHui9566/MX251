"""Parameterized tail factoring across memory models and optimization modes."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(args.support.resolve())
env['PATH'] = str(args.support.resolve() / 'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        directory = args.output / (model + '-' + opt)
        directory.mkdir()
        for path in tests.glob('shared-tails*'):
            shutil.copy2(path, directory / path.name)
        command = [str(args.sdcc.resolve()), '-mmcs251', '--model-' + model,
            '--opt-code-' + opt, '--stack-auto', '--mcs251-wide-ordinary',
            '--mcs251-machine-ir', '--mcs251-function-metadata']
        outputs = {}
        for variant in ('enabled', 'disabled'):
            runenv = dict(env, MX251_SHARED_TAILS='0' if variant == 'disabled' else '1')
            result = subprocess.run(command + ['-c', 'shared-tails-cases.c', '-o', variant+'.rel'],
                                    cwd=directory, env=runenv, capture_output=True)
            (directory / (variant+'.log')).write_bytes(result.stdout + result.stderr)
            assert not result.returncode, result.stderr
            outputs[variant] = (directory / (variant+'.asm')).read_text()
        selected = re.findall(r'^; wide shared-tail choice function=(\S+) groups=\d+ selected=1', outputs['enabled'], re.M)
        assert {'tail_pwm', 'tail_clock'} <= set(selected), selected
        assert not {'tail_cyclic', 'tail_call'} & set(selected), selected
        assert 'shared-tail choice' not in outputs['disabled']
        assert re.search(r'shared-tail trial arms=3 nodes=\d+ parameters=2', outputs['enabled'])
        assert re.search(r'shared-tail trial arms=4 nodes=\d+ parameters=4', outputs['enabled'])
        rows.append(dict(model=model, optimization=opt, selected=selected,
                         variants=list(outputs), cyclicAndCallBodiesRetained=True))
(args.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2)+'\n')
print('PASS: 4 configurations x 2 variants; three/four arms, volatile reads/writes, loop and call boundaries')
