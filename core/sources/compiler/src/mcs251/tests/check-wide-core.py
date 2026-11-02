#!/usr/bin/env python3
"""Compile the R30 corpus; board execution remains an explicit separate step."""
from pathlib import Path
import argparse, json, re, subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--sdcc', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=False)
rows = []
for model in ['small', 'large']:
    root = args.output_dir / model
    root.mkdir()
    (root / 'core.c').write_bytes(args.source.read_bytes())
    command = [str(args.sdcc.resolve()), '-mmcs251', '--model-' + model,
               '--stack-auto', '--mcs251-wide-backend', '--opt-code-speed',
               '-c', 'core.c']
    result = subprocess.run(command, cwd=root, capture_output=True)
    (root / 'compile.log').write_bytes(result.stdout + result.stderr)
    assert result.returncode == 0, result.stderr
    assembly = (root / 'core.asm').read_text()
    accepted = dict(re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)', assembly))
    fallback = re.findall(r'wide-mir fallback: (\w+)', assembly)
    assert len(accepted) == 33 and fallback == ['sign16'], (accepted, fallback)
    assert int(accepted['spill']) > 0 and int(accepted['byte_spill']) > 0
    byte_spill = assembly.split('wide-mir accepted: byte_spill;', 1)[1]
    assert '\tmov\tb,a' in byte_spill and '\tmov\ta,b' in byte_spill
    assert 'reg=24' in assembly and 'reg=28' in assembly
    rows.append(dict(model=model, command=command, accepted=accepted,
                     fallback=fallback, hardwareExecuted=False))
(args.output_dir / 'results.json').write_text(json.dumps(rows, indent=2))
print('PASS: both stack-auto models; 33 accepted functions, 1 explicit fallback')
