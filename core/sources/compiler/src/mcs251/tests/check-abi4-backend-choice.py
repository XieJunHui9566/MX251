"""Compare whole ABI4 functions with existing emitters and unchanged ABI2 output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdcc', type=Path, required=True)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--support', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
rows = []


def body(text, name, abi):
    symbol = ('___m251r4_' if abi == 4 else '_') + name
    match = re.search(r'^' + symbol + r':\s*$(.*?)^; mx251-function end', text, re.M | re.S)
    assert match, symbol
    lines = [s for line in match[1].splitlines()
             if (s := line.split(';', 1)[0].strip()) and not re.match(r'ar[0-7] = ', s)]
    return '\n'.join(lines)


def compile_at(cc, d, name, abi, model, opt, *, wide=True, env_options=None, preserve=False):
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_'))}
    env.update(SDCC_HOME=str(a.support.resolve()), MX251_WIDE_VALUE_LIMIT='512', MX251_WIDE_NODE_LIMIT='2048')
    env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
    env.update(env_options or {})
    cmd = [str(cc.resolve()), '-mmcs251', '--stack-auto', '--model-' + model, '--opt-code-' + opt,
           '--mcs251-native-abi', '--mcs251-native-int-runtime', '--mcs251-machine-ir',
           '--mcs251-function-metadata', '-DTEST_ABI=' + str(abi)]
    if wide:
        cmd += ['--mcs251-wide-ordinary']
    if preserve:
        (d / 'choice.preserve').write_text('MX251-CALLEE-PRESERVE-1\nglobal ___m251r4_mixed_calls 4 0f000000\n')
        cmd += ['--mcs251-callee-preserve', 'choice.preserve']
    cmd += ['-c', 'abi4-backend-choice.c', '-o', name + '.rel']
    q = subprocess.run(cmd, cwd=d, env=env, capture_output=True, timeout=60)
    (d / (name + '.log')).write_bytes(q.stdout + q.stderr)
    assert q.returncode == 0, (d, name, q.stderr)
    return (d / (name + '.asm')).read_text()


for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output_dir / (model + '-' + opt)
        d.mkdir()
        for name in ('abi4-backend-choice.c', 'abi4-backend-choice.h'):
            shutil.copy2(tests / name, d / name)
        for abi in (2, 4):
            prefix = 'abi' + str(abi) + '-'
            compile_at(a.baseline, d, prefix + 'parent', abi, model, opt)
            new = compile_at(a.sdcc, d, prefix + 'new', abi, model, opt)
            if abi == 2:
                assert (d / (prefix + 'parent.rel')).read_bytes() == (d / (prefix + 'new.rel')).read_bytes()
                continue
            compile_at(a.sdcc, d, prefix + 'disabled', abi, model, opt,
                       env_options={'MX251_LINEAR_BACKEND_CHOICE': '0'})
            assert (d / (prefix + 'parent.rel')).read_bytes() == (d / (prefix + 'disabled.rel')).read_bytes()
            forced = compile_at(a.sdcc, d, prefix + 'forced', abi, model, opt,
                                env_options={'MX251_LINEAR_BACKEND_CHOICE': '0', 'MX251_NARROW_UNSIGNED_NEGATE': '1'})
            legacy = compile_at(a.sdcc, d, prefix + 'legacy', abi, model, opt, wide=False)
            selected = {}
            for name in ('negate_word', 'negate_byte', 'negate_loop', 'divide_negate'):
                assert 'mx251-narrow-negate function=___m251r4_' + name + ' selected=wide' in new, (model, opt, name)
                assert body(new, name, abi) == body(forced, name, abi), (model, opt, name)
                selected[name] = 'wide'
            assert 'mx251-body-choice function=___m251r4_mixed_calls selected=legacy' in new, (model, opt)
            assert body(new, 'mixed_calls', abi) == body(legacy, 'mixed_calls', abi), (model, opt)
            selected['mixed_calls'] = 'legacy'
            kept = compile_at(a.sdcc, d, prefix + 'preserved', abi, model, opt, preserve=True)
            kept_body = re.search(r'^___m251r4_mixed_calls:\s*$(.*?)^; mx251-function end', kept, re.M | re.S)[1]
            assert 'mx251-callee-preserved schema=1 saved=0f000000' in kept_body
            assert re.search(r'push\s+dr24', kept_body) and re.search(r'pop\s+dr24', kept_body)
            assert 'mx251-body-choice function=___m251r4_mixed_calls selected=legacy' not in kept
            sizes = {label: int(re.search(r'^A CSEG size ([0-9A-F]+)',
                     (d / (prefix + label + '.rel')).read_text(), re.M)[1], 16) for label in ('parent', 'new')}
            assert sizes['new'] < sizes['parent'], sizes
            rows.append(dict(model=model, optimization=opt, selected=selected, bytes=sizes,
                             abi2Unchanged=True, optOutMatchesParent=True, existingEmitterBodiesMatch=True,
                             calleePreservationRetained=True))
(a.output_dir / 'results.json').write_text(json.dumps(dict(
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(), configurations=rows), indent=2) + '\n')
print('PASS: 4 configurations, ABI2 equivalence, ABI4 opt-out, both complete emitter choices and callee preservation')
