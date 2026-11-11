"""Default immediate word comparisons, signed/unsigned and explicit opt-out."""
import argparse
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
env = {k:v for k,v in os.environ.items() if not k.startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        root = a.output/(model+'-'+opt); root.mkdir()
        shutil.copy2(Path(__file__).with_name('word-compare-cases.c'), root/'cases.c')
        texts = {}
        for variant, compiler, flags in (
            ('default', a.sdcc, []),
            ('disabled', a.sdcc, ['--no-mcs251-word-compare-immediate']),
            ('baseline', a.baseline, [])):
            command = [str(compiler.resolve()), '-mmcs251', '--model-'+model,
                '--opt-code-'+opt, '--stack-auto', '--mcs251-wide-ordinary',
                '--mcs251-machine-ir', '--mcs251-function-metadata', *flags,
                '-c', 'cases.c', '-o', variant+'.rel']
            r = subprocess.run(command, cwd=root, env=env, capture_output=True)
            (root/(variant+'.log')).write_bytes(r.stdout+r.stderr)
            assert not r.returncode, r.stderr
            texts[variant] = (root/(variant+'.asm')).read_text()
        assert (root/'disabled.rel').read_bytes() == (root/'baseline.rel').read_bytes()
        count = len(re.findall(r'wide word immediate compare', texts['default']))
        assert count >= 12, (model, opt, count)
        assert 'wide word immediate compare' not in texts['disabled']
        bodies = dict(re.findall(r'; mx251-function begin[^\n]*symbol=(\S+)[^\n]*\n(.*?); mx251-function end', texts['default'], re.S))
        for name in ('_word_register', '_word_dword'):
            assert 'wide word immediate compare' not in bodies[name], name
        volatile = bodies['_word_volatile']
        loads = re.findall(r'^\s*mov\s+\w+,\s*([^;\n]*_word_sample[^;\n]*)', volatile, re.M)
        old_loads = re.findall(r'^\s*mov\s+\w+,\s*([^;\n]*_word_sample[^;\n]*)', texts['disabled'], re.M)
        assert len(loads) == 2 and loads == old_loads, (loads, old_loads)
        rows.append(dict(model=model, optimization=opt, immediateComparisons=count,
                         optOutObjectMatchesBaseline=True, volatileByteReads=loads))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2)+'\n')
print('PASS: 4 configurations, signed/unsigned comparisons, volatile reads and baseline opt-out')
