"""Check quotient reuse and distinct snapshots/control-flow boundaries."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdcc', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=False)
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        out = a.output_dir / (model + '-' + opt)
        out.mkdir()
        (out / 'test.c').write_bytes(a.source.read_bytes())
        command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--stack-auto',
                   '--opt-code-' + opt, '--mcs251-wide-backend', '--mcs251-native-int-runtime',
                   '-c', 'test.c']
        run = subprocess.run(command, cwd=out, capture_output=True)
        (out / 'compile.log').write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, run.stderr
        asm = (out / 'test.asm').read_text()
        functions = re.split(r'; wide imported function=(\w+)[^\n]*\n', asm)
        bodies = dict(zip(functions[1::2], functions[2::2]))
        for name in ('pair_unsigned', 'pair_signed', 'pair_twice'):
            assert ('wide quotient-reuse' in bodies[name]) == (opt == 'speed'), name
            if opt == 'speed':
                assert not re.search(r'\becall\s+___m251r4__mod[su]long', bodies[name]), name
        for name in ('different_divisor', 'different_dividend', 'branch_division',
                     'volatile_divisor', 'separate_blocks', 'quotient_only', 'remainder_only'):
            assert 'wide quotient-reuse' not in bodies[name], name
        assert 'wide-mir fallback' not in asm
        rows.append(dict(model=model, optimization=opt, status='PASS', functions=len(bodies)))
        # No source-level multiplication: the inserted helper must still assemble.
        (out / 'minimal.c').write_text('typedef unsigned long u32;\n'
            'u32 pair(u32 a,u32 b) __reentrant __sdcccall(4) {u32 q=a/b;return q^(a%b);}\n')
        minimal = subprocess.run(command[:-1] + ['minimal.c'], cwd=out, capture_output=True)
        assert minimal.returncode == 0, minimal.stderr
(a.output_dir / 'result.json').write_bytes((json.dumps(rows, indent=2) + '\n').encode())
print('PASS: quotient reuse and operand/control-flow boundaries in both models and optimization modes')
