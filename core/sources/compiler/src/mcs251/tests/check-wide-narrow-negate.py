"""Compare default selection to the existing opt-in emitter and legacy output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--sdcc', required=True, type=Path)
p.add_argument('--baseline', required=True, type=Path)
p.add_argument('--output-dir', required=True, type=Path)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=False)
source = Path(__file__).with_name('wide-narrow-negate.c').read_text()
functions = re.findall(r'^\w+ (\w+)\(', source, re.M)
rows = []

def body(text, name):
    start = text.index('_' + name + ':\n')
    end = len(text)
    for other in functions:
        at = text.find('_' + other + ':\n', start + len(name) + 3)
        if at >= 0:
            end = min(end, at)
    return '\n'.join(s.strip() for line in text[start:end].splitlines()
                     if (s := line.split(';', 1)[0].strip()) and not re.match(r'ar[0-7] = ', s))

def compile_at(cc, d, name, flags, mode=None, division=False):
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(('MX251_', 'MCS251_'))}
    env['SDCC_HOME'] = str(cc.resolve().parent.parent)
    if mode is not None:
        env['MX251_NARROW_UNSIGNED_NEGATE'] = mode
    env['MX251_WIDE_UNSIGNED_DIVISION'] = '1' if division else '0'
    cmd = [str(cc.resolve()), '-mmcs251', '--stack-auto',
           '--mcs251-wide-ordinary', *flags, '-c', 'probe.c', '-o', name + '.rel']
    q = subprocess.run(cmd, cwd=d, env=env, capture_output=True, timeout=60)
    (d / (name + '.log')).write_bytes(q.stdout + q.stderr)
    assert q.returncode == 0, (d, q.stderr)
    return (d / (name + '.asm')).read_text()

for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output_dir / (model + '-' + opt)
        d.mkdir()
        (d / 'probe.c').write_text(source)
        flags = ['--model-' + model, '--opt-code-' + opt]
        parent = compile_at(a.baseline, d, 'parent', flags)
        compile_at(a.baseline, d, 'parent-disabled', flags, '0')
        forced = compile_at(a.baseline, d, 'forced', flags, '1')
        candidate = compile_at(a.sdcc, d, 'candidate', flags)
        disabled = compile_at(a.sdcc, d, 'disabled', flags, '0')
        assert (d / 'parent-disabled.rel').read_bytes() == (d / 'disabled.rel').read_bytes()
        for name in ('nn', 'baud_reload', 'loop_negate'):
            assert 'mx251-narrow-negate function=_' + name + ' selected=wide' in candidate
            assert body(candidate, name) == body(forced, name), name
        assert 'mx251-narrow-negate function=_nb selected=legacy' in candidate
        assert body(candidate, 'nb') == body(parent, 'nb')
        sizes = {}
        for name in ('parent', 'candidate'):
            rel = (d / (name + '.rel')).read_text()
            sizes[name] = int(re.search(r'^A CSEG size ([0-9A-F]+)', rel, re.M)[1], 16)
        assert sizes['candidate'] < sizes['parent'], sizes
        # Revisit the earlier growing arithmetic fixture with its division
        # experiment enabled, using today's emitter and whole-function selection.
        if opt == 'speed':
            div_parent = compile_at(a.baseline, d, 'division-parent', flags, division=True)
            div_new = compile_at(a.sdcc, d, 'division-candidate', flags, division=True)
            div_forced = compile_at(a.baseline, d, 'division-forced', flags, '1', division=True)
            assert 'mx251-narrow-negate function=_nb selected=legacy' in div_new
            assert body(div_parent, 'nb') == body(div_new, 'nb')
            assert body(div_forced, 'np') == body(div_new, 'np')
            sizes_div = [int(re.search(r'^A CSEG size ([0-9A-F]+)',
                         (d / (name + '.rel')).read_text(), re.M)[1], 16)
                         for name in ('division-parent', 'division-candidate')]
            assert sizes_div[1] <= sizes_div[0], sizes_div
        rows.append(dict(model=model, optimization=opt, **sizes,
                         selectedWideMatchesExistingEmitter=True,
                         cheaperByteNegationMatchesParent=True,
                         loopNegationMatchesExistingEmitter=True))
(a.output_dir / 'results.json').write_text(json.dumps(dict(
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
    configurations=rows, hardwareExecuted=False), indent=2) + '\n')
print('PASS', len(rows), 'configurations: selection, existing emitter equivalence, loops and opt-out')
