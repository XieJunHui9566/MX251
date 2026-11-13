"""Keep requested register preservation when choosing a complete backend."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'baseline', 'support', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = dict(os.environ, SDCC_HOME=str(a.support.resolve()))
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']

def instructions(text):
    return [line.split(';', 1)[0].strip() for line in text.splitlines()
            if line.split(';', 1)[0].strip()]

rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        outputs = {}
        for variant in ('parent', 'disabled', 'enabled', 'plain-parent', 'plain-new'):
            d = a.output/(model+'-'+opt)/variant
            d.mkdir(parents=True)
            for name in ('machine-board.h', 'body-choice-cases.c'):
                shutil.copy2(tests/name, d/name)
            (d/'preserve.txt').write_text('MX251-CALLEE-PRESERVE-1\nglobal _apply_feature 2 0f000000\n')
            compiler = a.baseline if variant in ('parent', 'plain-parent') else a.sdcc
            cmd = [str(compiler.resolve()), '-mmcs251', '--model-'+model, '--opt-code-'+opt,
                   '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
                   '--mcs251-function-metadata']
            if not variant.startswith('plain-'):
                cmd += ['--mcs251-callee-preserve', 'preserve.txt']
            cmd += ['-c', 'body-choice-cases.c']
            q = subprocess.run(cmd, cwd=d, env=dict(env, MX251_PRESERVED_BACKEND='0' if variant=='disabled' else '1'), capture_output=True)
            (d/'compile.log').write_bytes(q.stdout+q.stderr)
            assert not q.returncode, q.stderr
            outputs[variant] = (d/'body-choice-cases.asm').read_text()
            rows.append(dict(variant=variant, command=cmd, directory=str(d)))
        assert instructions(outputs['parent']) == instructions(outputs['disabled'])
        assert instructions(outputs['plain-parent']) == instructions(outputs['plain-new'])
        assert '; mx251-body-choice function=_apply_feature selected=legacy' in outputs['parent']
        body = outputs['enabled'].split('symbol=_apply_feature ', 1)[1].split('; mx251-function end', 1)[0]
        assert '; mx251-preserved-backend function=_apply_feature selected=wide' in body
        assert '; mx251-callee-preserved schema=1 saved=0f000000' in body
        assert re.search(r'push\s+dr24', body) and re.search(r'pop\s+dr24', body)
        assert '; mx251-body-choice function=_apply_feature selected=legacy' not in body
report = dict(status='PASS', configurations=4, variants=5, commands=rows,
              compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
              parentCompilerSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest())
(a.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
print('PASS: preservation survives backend choice; disabled and unrequested code match parent in four configurations')
