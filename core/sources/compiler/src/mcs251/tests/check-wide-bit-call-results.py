"""Compile/assemble ABI2 carry results; hardware execution is recorded separately."""
import argparse
import hashlib
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
source = Path(__file__).with_name('wide-bit-call-results.c').read_text()
rows = []

def compile_at(cc, d, extra, name):
    cmd = [str(cc.resolve()), '-mmcs251', '--stack-auto', *extra, '-c', 'probe.c', '-o', name + '.rel']
    q = subprocess.run(cmd, cwd=d, capture_output=True, timeout=60)
    (d / (name + '.log')).write_bytes(q.stdout + q.stderr)
    (d / (name + '-command.json')).write_text(json.dumps(cmd, indent=2))
    assert q.returncode == 0, (d, q.stderr)
    return (d / (name + '.asm')).read_text()

for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        for feedback in (False, True):
            d = a.output_dir / f'{model}-{opt}-{feedback}'
            d.mkdir()
            (d / 'probe.c').write_text(source)
            # Unknown providers must remain fully clobbering. Feedback takes
            # the same conservative bound; no test-invented preservation claim.
            (d / 'effects.txt').write_text('MX251-LEAF-EFFECTS-1\n' + ''.join(
                f'_{s} 2 ffffffff\n' for s in ('flag0', 'flag3', 'flag6')))
            flags = ['--model-' + model, '--opt-code-' + opt]
            wide = flags + ['--mcs251-wide-ordinary']
            if feedback:
                wide += ['--mcs251-leaf-effects', 'effects.txt']
            asm = compile_at(a.sdcc, d, wide, 'candidate')
            accepted = re.findall(r'wide-mir accepted: (bit_call_\w+);', asm)
            assert len(accepted) == 6, (d, accepted)
            # Existing whole-function cost selection retains the cheaper legacy
            # bit-object store. Do not force the wide backend for this case.
            assert 'function=_bit_call_store selected=legacy' in asm
            wide_asm = re.sub(r'_bit_call_store:\n.*?(?=_bit_call_pressure:)', '', asm, flags=re.S)
            calls = re.findall(r'\tecall\t_flag[036]\n(.*?)(?=\tecall\t|\teret\b)', wide_asm, re.S)
            captures = []
            for body in calls:
                if 'wide ordinary result unused:' in body:
                    continue
                match = re.match(r'; wide ordinary bit result: CY to byte before cleanup\n'
                                 r'\tclr\ta\n\trlc\ta\n\tmov\t([^\n]+)', body)
                assert match, (d, body[:400])
                captures.append(match[1])
            assert len(captures) == 20, (d, len(captures))
            assert any(s.startswith('@spx') for s in captures), 'must exercise actual result spills'
            assert any(s.startswith('r') for s in captures), 'must exercise register results'
            assert '\tsub\tspx,#3' in asm and '\tsub\tspx,#6' in asm
            assert 'wide ordinary result forwarded' not in asm, 'CY cannot forward as DPL'
            assert asm.count('wide ordinary result unused:') == 1
            compile_at(a.baseline, d, flags, 'disabled-parent')
            compile_at(a.sdcc, d, flags, 'disabled-candidate')
            assert (d / 'disabled-parent.rel').read_bytes() == (d / 'disabled-candidate.rel').read_bytes()
            rows.append(dict(model=model, optimization=opt, feedback=feedback,
                             accepted=len(accepted), captures=len(captures),
                             resultSpills=sum(s.startswith('@spx') for s in captures),
                             disabledObjectIdentical=True))

(a.output_dir / 'results.json').write_text(json.dumps(dict(
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
    configurations=rows, hardwareExecuted=False), indent=2) + '\n')
print('PASS', len(rows), 'configurations: CY capture, stack cleanup, spills, dead results and unchanged fallback')
