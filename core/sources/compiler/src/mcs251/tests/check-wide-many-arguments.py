"""Sixteen mixed parameters through ABI2/4 and the unchanged legacy ABI."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdcc', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parent
rows = []
widths = [1, 2, 1, 2, 4, 1, 2, 4, 3, 1, 2, 4, 3, 1, 2, 4]
for model in ('small', 'large'):
    for abi in (2, 4):
        d = a.output_dir / f'{model}-abi{abi}'
        d.mkdir()
        common = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model,
                  '--stack-auto', '--mcs251-native-abi', '--opt-code-speed',
                  f'-DMANY_ABI={abi}', '-I' + str(source)]
        outputs = {}
        for part in ('caller', 'callee', 'legacy'):
            wide = [] if part == 'legacy' else ['--mcs251-wide-ordinary']
            r = subprocess.run(common + wide + ['-c',
                str(source / f'wide-many-arguments-{part}.c'), '-o', part + '.rel'],
                cwd=d, capture_output=True)
            (d / (part + '.log')).write_bytes(r.stdout + r.stderr)
            assert r.returncode == 0, (model, abi, part, r.stderr)
            outputs[part] = (d / (part + '.asm')).read_text()
        assert 'wide-mir accepted: many_mix;' in outputs['callee']
        assert 'wide-mir accepted: compute;' in outputs['caller']
        assert 'wide-mir accepted:' not in outputs['legacy']
        registers = 1 if abi == 2 else 2
        total = sum(widths[registers:])
        assert f'wide stack call args={16-registers} bytes={total}' in outputs['caller']
        offset, expected = -2, set()
        for width in widths[registers:]:
            offset -= width
            expected.add(offset)
        actual = set(map(int, re.findall(r'entry-offset=(-?\d+)', outputs['callee'])))
        assert actual == expected, (actual, expected)
        assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b', outputs['callee'])
        rows.append(dict(model=model, abi=abi, parameters=16,
                         stackBytes=total, entryOffsets=sorted(actual),
                         callerAccepted=True, calleeAccepted=True))
(a.output_dir / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
print('PASS 16 mixed parameters, ABI2/4, small/large, native and legacy boundaries')
