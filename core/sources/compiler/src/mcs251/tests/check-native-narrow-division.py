"""Compile native unsigned byte/word division across models and calling ABIs."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdcc', type=Path, required=True)
p.add_argument('--toolchain', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.toolchain.resolve())
env['PATH'] = str(a.toolchain.resolve() / 'bin') + ';' + env['PATH']
rows = []
names = ['div_q8', 'div_r8', 'div_q16', 'div_r16', 'div_narrow_q', 'div_narrow_r',
         'div_mixed', 'div_byte_word', 'div_decimal', 'div_live', 'div_volatile']
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        for abi in ('ordinary', 'native'):
            root = a.output / (model + '-' + opt + '-' + abi)
            root.mkdir()
            for name in ('native-narrow-division.c', 'native-narrow-division.h'):
                shutil.copy2(Path(__file__).with_name(name), root / name)
            command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model,
                       '--opt-code-' + opt, '--stack-auto', '--mcs251-machine-ir',
                       '--mcs251-wide-ordinary' if abi == 'ordinary' else '--mcs251-wide-backend']
            if abi == 'native':
                command += ['--mcs251-native-abi', '-DDIV_ABI=__reentrant __sdcccall(4)']
            run = subprocess.run(command + ['-c', 'native-narrow-division.c'], cwd=root, env=env, capture_output=True)
            (root / 'compile.log').write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            asm = (root / 'native-narrow-division.asm').read_text(encoding='latin1')
            accepted = re.findall(r'wide-mir accepted: (\w+)', asm)
            selected_legacy = re.findall(r'mx251-backend-choice function=_(\w+) selected=legacy', asm)
            assert set(names) <= set(accepted + selected_legacy), (root, accepted, selected_legacy)
            assert 'wide unsigned byte division' in asm and 'wide unsigned word division' in asm
            assert 'wide-mir fallback' not in asm
            assert not re.search(r'\br(?:1[6-9]|2\d|3[01])\b', asm)
            frame = re.search(r'wide-mir accepted: div_live;[^\n]* frame=(\d+)', asm)
            rows.append(dict(model=model, optimization=opt, abi=abi, accepted=len(accepted), selectedLegacy=selected_legacy,
                             liveFrameBytes=int(frame[1]), command=command))
            # Signed division and literal zero retain their pre-existing lowering.
            (root / 'boundary.c').write_text('unsigned int zero(unsigned int a) {return a/0u;}\n'
                                           'int signed_q(int a,int b) {return a/b;}\n')
            run = subprocess.run(command + ['-c', 'boundary.c'], cwd=root, env=env, capture_output=True)
            (root / 'boundary.log').write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            boundary = (root / 'boundary.asm').read_text(encoding='latin1')
            assert 'wide unsigned word division' not in boundary
            assert 'wide unsigned byte division' not in boundary
            disabled = dict(env, MX251_WIDE_UNSIGNED_DIVISION='0')
            run = subprocess.run(command + ['-c', 'native-narrow-division.c', '-o', 'disabled.rel'],
                                 cwd=root, env=disabled, capture_output=True)
            (root / 'disabled.log').write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            old = (root / 'disabled.asm').read_text(encoding='latin1')
            assert 'wide unsigned byte division' not in old and 'wide unsigned word division' not in old
(a.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf8', newline='\n')
print('PASS', len(rows), 'model/optimization/ABI combinations; mixed widths, live values, volatile and opt-out')
