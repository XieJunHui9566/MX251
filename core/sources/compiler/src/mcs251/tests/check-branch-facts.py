"""Check shared-middle-end folding before either backend receives the IR."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small', 'large'):
    for backend in ('legacy', 'wide'):
        variants = {}
        for enabled in (False, True):
            d = a.output / (model + '-' + backend + ('-on' if enabled else '-off'))
            d.mkdir()
            for name in ('branch-facts.c', 'machine-board.h'):
                shutil.copy2(tests / name, d / name)
            env['MX251_BRANCH_FACTS'] = '1' if enabled else '0'
            command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model,
                       '--stack-auto', '--opt-code-speed', '--mcs251-machine-ir',
                       '--dump-i-code', '-c', 'branch-facts.c']
            if backend == 'wide':
                command += ['--mcs251-wide-ordinary']
            q = subprocess.run(command, cwd=d, env=env, capture_output=True)
            (d / 'compile.log').write_bytes(q.stdout + q.stderr)
            assert q.returncode == 0, q.stderr
            text = (d / 'branch-facts.dumpgenconstprop_1').read_text()
            functions = {m[1]: m[2] for m in re.finditer(r'\tproc _(bf_\w+)(.*?)\teproc _\1\b', text, re.S)}
            assert len(functions) == 13, functions.keys()
            shifts = {name: body.count(' << ') for name, body in functions.items()}
            variants[enabled] = shifts
            assert (d / 'branch-facts.rel').is_file() and (d / 'branch-facts.lst').is_file()
            if enabled:
                for name in ('bf_mask', 'bf_guard', 'bf_signed'):
                    assert shifts[name] == 0, (name, shifts)
                # Two case values share a target; default also has a hole.
                assert 'jtab' in functions['bf_switch'] and shifts['bf_switch'] == 2
                # Old snapshots survive assignments and loop back edges.
                assert shifts['bf_join'] == 2 and shifts['bf_loop'] == 2
                # A truncated comparison must not determine the whole word.
                assert ' >> ' in functions['bf_cast']
                # The second volatile load is a new value, not the tested one.
                assert shifts['bf_memory'] == 1
        for name in ('bf_mask', 'bf_guard', 'bf_signed', 'bf_switch', 'bf_state'):
            assert variants[True][name] < variants[False][name], (model, backend, name, variants)
        rows.append(dict(model=model, backend=backend, variableShiftsBefore=variants[False],
                         variableShiftsAfter=variants[True]))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2) + '\n')
print('PASS: small/large x legacy/wide; folding, normalized jump table, shared targets, redefinitions, loops and truncation')
