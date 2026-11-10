"""Exercise cursor selection, byte-read scoping and unsupported loop shapes."""
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
        for path in tests.glob('loop-cursors*'):
            shutil.copy2(path, directory / path.name)
        command = [str(args.sdcc.resolve()), '-mmcs251', '--model-' + model,
            '--opt-code-' + opt, '--stack-auto', '--mcs251-wide-ordinary',
            '--mcs251-machine-ir', '--mcs251-function-metadata']
        outputs = {}
        for variant in ('enabled', 'disabled', 'movc'):
            runenv = dict(env, MX251_LOOP_CURSORS='0' if variant == 'disabled' else '1')
            extra = [] if variant == 'movc' else ['--mcs251-flat-code-reads']
            result = subprocess.run(command + extra + ['-c', 'loop-cursors-cases.c', '-o', variant+'.rel'],
                                    cwd=directory, env=runenv, capture_output=True)
            (directory / (variant+'.log')).write_bytes(result.stdout + result.stderr)
            assert not result.returncode, result.stderr
            outputs[variant] = (directory / (variant+'.asm')).read_text()
        selected = re.findall(r'loop-cursor choice function=(\S+) loops=\d+ selected=1', outputs['enabled'])
        assert 'cursor_copy' in selected, selected
        assert 'cursor_fill' in selected, selected
        for name in ('cursor_dynamic', 'cursor_call', 'cursor_ordered', 'cursor_wrap', 'cursor_parameter'):
            assert name not in selected, name
        assert 'loop-cursor choice' not in outputs['disabled']
        assert 'native code load width=1' in outputs['enabled']
        assert 'native code load width=1' not in outputs['disabled']
        assert 'native code load width=1' not in outputs['movc']
        rows.append(dict(model=model, optimization=opt, selected=selected,
                         variants=list(outputs), shapeAndTargetChecks=True))
(args.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2)+'\n')
print('PASS: 4 configurations, 3 variants each; multi-stream copy, 16-bit counter, target and shape checks')
