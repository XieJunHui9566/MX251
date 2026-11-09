"""Call-frame placement under both public ABIs and memory/code objectives."""
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
for model in ['small', 'large']:
    for objective in ['speed', 'size']:
        for abi in ['native', 'ordinary']:
            d = a.output_dir / (model + '-' + objective + '-' + abi)
            d.mkdir()
            for n in ['machine-call-plan.h', 'machine-call-plan.c', 'machine-call-ordinary.c']:
                (d / n).write_bytes((source / n).read_bytes())
            file = 'machine-call-plan.c' if abi == 'native' else 'machine-call-ordinary.c'
            sizes = []
            for tag, cc in [('parent', a.baseline), ('candidate', a.sdcc)]:
                cmd = [str(cc.resolve()), '-mmcs251', '--model-' + model, '--stack-auto',
                       '--opt-code-' + objective, '--mcs251-wide-ordinary',
                       '--mcs251-function-metadata', '-c', file, '-o', tag + '.rel']
                r = subprocess.run(cmd, cwd=d, capture_output=True)
                (d / (tag + '.log')).write_bytes(r.stdout + r.stderr)
                assert r.returncode == 0, r.stderr
                rel = (d / (tag + '.rel')).read_text()
                sizes.append(int(re.search(r'^A CSEG size ([0-9A-Fa-f]+)', rel, re.M)[1], 16))
            asm = (d / 'candidate.asm').read_text()
            assert 'wide call-frame store' in asm and 'wide call-frame load' in asm
            assert sizes[1] < sizes[0], (model, objective, abi, sizes)
            regions = re.split(r'; mcs251 wide-mir accepted: ', asm)[1:]
            plans = []
            for body in regions:
                body = body.split('; mx251-function end', 1)[0]
                frame = int(re.search(r'frame=(\d+)', body)[1])
                slots = {}
                for op, value, reg, width, slot in re.findall(
                        r'wide call-frame (load|store) value=(\d+) reg=(\d+) width=(\d+) slot=(\d+)', body):
                    v, r, w, s = map(int, [value, reg, width, slot])
                    assert s >= 0 and s + w <= frame
                    assert slots.setdefault(v, (r, w, s)) == (r, w, s)
                for v, (_, w, s) in slots.items():
                    for other, (_, z, t) in slots.items():
                        if v != other:
                            assert set(range(s, s + w)).isdisjoint(range(t, t + z))
                if slots:
                    plans.append(dict(function=body.split(';', 1)[0], frame=frame, values=len(slots)))
            rows.append(dict(model=model, objective=objective, abi=abi,
                             parentBytes=sizes[0], candidateBytes=sizes[1], plans=plans))
(a.output_dir / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
print('PASS:', len(rows), 'ABI/model/objective configurations')
for row in rows:
    print(row['model'], row['objective'], row['abi'], row['parentBytes'], '->', row['candidateBytes'])
