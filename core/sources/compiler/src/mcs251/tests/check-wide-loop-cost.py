"""Independent generated-cycle counts and unsupported-control-flow rejection."""
import argparse
import json
from pathlib import Path
import random
import re
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('--probe', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
records = []


def check(name, parent, candidate, expected=None, unknown=False):
    paths = [a.output / (name + '-' + k + '.asm') for k in ('parent', 'candidate')]
    for p, text in zip(paths, (parent, candidate)):
        p.write_text(text, encoding='utf8')
    run = subprocess.run([str(a.probe.resolve()), *map(str, paths)], capture_output=True, timeout=15)
    if run.returncode:
        raise AssertionError((name, run.returncode, run.stderr))
    result = json.loads(run.stdout)
    if unknown:
        assert result['candidate']['unknown'] and result['decision']==-1 and not result['notWorse'], (name, result)
    else:
        assert not result['parent']['unknown'] and not result['candidate']['unknown'], (name, result)
        old, new = expected
        assert [l['metrics'] for l in result['parent']['loops']] == old, (name, result)
        assert [l['metrics'] for l in result['candidate']['loops']] == new, (name, result)
        assert result['notWorse'] == (len(old) == len(new) and all(all(y <= x for x, y in zip(p, c)) for p, c in zip(old, new)))
    records.append(dict(name=name, result=result))


def program(counts, prefix='loop', trampoline=False):
    text = 'entry:\n\tmov\tr0,#4\n'
    costs = []
    for k, (moves, reads, pushes) in enumerate(counts):
        label, done, body = f'{prefix}_{k}', f'done_{k}', f'body_{k}'
        text += label + ':\n'
        if trampoline:
            text += f'\tcmp\tr0,#0\n\tjc\t{body}\n\tejmp\t{done}\n{body}:\n'
        text += '\tmov\tr4,r5\n' * moves
        text += '\tmov\twr4,@spx-2\n' * reads
        text += '\tpush\tr6\n\tpop\tr6\n' * pushes
        if not trampoline:
            text += f'\tcmp\tr0,#0\n\tjnc\t{done}\n'
        text += f'\tejmp\t{label}\n{done}:\n'
        costs.append([moves + reads + 2 * pushes + 3, moves + reads, reads, reads, 0, pushes, pushes])
    return text + '\teret\n', costs


rng = random.Random(137)
for k in range(100):
    count = rng.randrange(1, 5)
    old_counts = [tuple(rng.randrange(4) for _ in range(3)) for _ in range(count)]
    new_counts = [tuple(rng.randrange(4) for _ in range(3)) for _ in range(count)]
    p, old = program(old_counts, trampoline=bool(k & 1))
    c, new = program(new_counts, trampoline=bool(k & 1))
    check('generated-' + str(k), p, c, (old, new))

p, old = program([(2, 1, 0)])
unsupported = {
    'unknown-instruction': p.replace('\tmov\tr4,r5', '\tmystery\tr4,r5', 1),
    'unresolved-label': p.replace('ejmp\tloop_0', 'ejmp\tmissing'),
    'duplicate-label': p + 'loop_0:\n',
    'call-in-cycle': p.replace('\tmov\tr4,r5', '\tecall\t_other', 1),
    'nested-cycle': p.replace('\tmov\tr4,r5', 'inner:\n\tinc\tr1\n\tjne\tinner', 1),
    'external-entry': p.replace('entry:\n', 'entry:\n\tje\tmiddle\n').replace('\tmov\twr4,@spx-2', 'middle:\n\tmov\twr4,@spx-2'),
    'two-latches': p.replace('\tmov\tr4,r5', '\tje\tloop_0', 1),
    'indirect-jump': p.replace('ejmp\tloop_0', 'ejmp\t@a+dptr'),
    'instruction-bound': 'entry:\n' + '\tmov\tr4,r5\n' * 2049 + '\teret\n',
    'loop-bound': program([(1, 0, 0)] * 33)[0],
    'long-label': p.replace('loop_0', 'x' * 161),
}
for name, text in unsupported.items():
    check(name, p, text, unknown=True)

# 同一循环可用直接条件回边；额外外围指令不进入迭代成本。
direct = 'start:\nL:\n\tmov\tr4,r5\n\tcmp\tr0,#0\n\tjc\tL\n\teret\n'
check('direct-conditional-backedge', direct, direct, ([[3, 1, 0, 0, 0, 0, 0]],) * 2)
for opcode in ('jz','jnz'):
    branch=direct.replace('\tjc\tL', '\t'+opcode+'\tL')
    check('accumulator-'+opcode, branch, branch, ([[3, 1, 0, 0, 0, 0, 0]],) * 2)
check('acyclic', 'entry:\n\tmov\tr0,r1\n\teret\n', 'entry:\n\teret\n', ([], []))
(a.output / 'results.json').write_text(json.dumps(dict(passed=True, cases=records), indent=2) + '\n')
print(f'PASS {len(records)} cases: generated cycle vectors, exits, bounds and conservative unknowns')
