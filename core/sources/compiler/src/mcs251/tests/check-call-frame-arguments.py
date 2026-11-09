"""Check direct call operands across physical widths and both callee ABIs."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--sdcc', required=True, type=Path)
p.add_argument('--baseline', required=True, type=Path)
p.add_argument('--output-dir', required=True, type=Path)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=False)
source = Path(__file__).parent
rows = []
for model in ('small', 'large'):
    for objective in ('speed', 'size'):
        d = a.output_dir / (model + '-' + objective)
        d.mkdir()
        for name in ('machine-call-plan.h', 'call-frame-arguments.h', 'call-frame-arguments.c'):
            (d / name).write_bytes((source / name).read_bytes())
        sizes = []
        for tag, compiler in [('parent', a.baseline), ('candidate', a.sdcc)]:
            cmd = [str(compiler.resolve()), '-mmcs251', '--model-' + model,
                   '--stack-auto', '--opt-code-' + objective, '--mcs251-wide-ordinary',
                   '-c', 'call-frame-arguments.c', '-o', tag + '.rel']
            r = subprocess.run(cmd, cwd=d, capture_output=True)
            (d / (tag + '.log')).write_bytes(r.stdout + r.stderr)
            assert r.returncode == 0, r.stderr
            rel = (d / (tag + '.rel')).read_text()
            sizes.append(int(re.search(r'^A CSEG size ([0-9A-Fa-f]+)', rel, re.M)[1], 16))
        asm = (d / 'candidate.asm').read_text()
        assert sizes[1] < sizes[0], sizes
        for name in ('byte', 'word', 'dword', 'native', 'pointer'):
            body = asm.split('wide-mir accepted: args_' + name + ';', 1)[1]
            body = body.split('; mcs251 wide-mir', 1)[0]
            assert 'wide call-frame argument' in body, name
        for width in (1, 2, 3, 4):
            assert 'wide ordinary argument direct stack physical=' + str(width) in asm, width
        assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b', asm)
        rows.append(dict(model=model, objective=objective,
                         parentBytes=sizes[0], candidateBytes=sizes[1],
                         directArguments=asm.count('wide call-frame argument')))
(a.output_dir / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
print(json.dumps(rows, indent=2))
