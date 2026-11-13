"""Dead copies across peripheral inline blocks; keep live values and I/O order."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'baseline', 'toolchain', 'output'):
    p.add_argument('--' + key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.toolchain.resolve())
env['PATH'] = str(a.toolchain.resolve() / 'bin') + ';' + env['PATH']
cases = {
    'dead-copy': ('clr 0xe7.6', True),
    'ordered-block': ('clr 0xe7.6\nsetb 0xe6.5\nsetb 0xe6.5\ncpl 0xe6.4', True),
    'live-byte': ('clr 0xe7.6\nmov _sink,r7', False),
    'live-word-alias': ('clr 0xe7.6\nmov _sink,wr6', False),
    'live-dword-alias': ('clr 0xe7.6\nmov dr0,dr4', False),
    'stack': ('clr 0xe7.6\nmov spx,dr0', False),
    'mapping': ('setb 0xba.7', False),
    'power': ('setb 0x87.1', False),
    'local-label': ('clr 0xe7.6\nsjmp 00199$\n00199$:\nsetb 0xe7.6', False),
    'fixed-pc': ('clr 0xe7.6\nsjmp .+2', False),
}


def instructions(text):
    body = text.split('_work:', 1)[1].split('.area', 1)[0]
    return [re.sub(r'\s+', ' ', s) for line in body.splitlines()
            if (s := line.split(';', 1)[0].strip()) and not re.match(r'ar\d+\s*=', s)]


rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        for name, (inline, removable) in cases.items():
            root = a.output / (model + '-' + opt + '-' + name)
            root.mkdir()
            (root / 'test.c').write_text(
                '#define control (*(volatile __xdata unsigned char *)0x12001UL)\n'
                'volatile __data unsigned int sink;\n'
                'void work(void) { control &= ~3; __asm\n' + inline +
                '\n__endasm; for(;;); }\n', encoding='utf8')
            variants = {}
            for label, compiler in (('parent', a.baseline), ('candidate', a.sdcc)):
                run = subprocess.run([str(compiler.resolve()), '-mmcs251', '--model-' + model,
                    '--opt-code-' + opt, '--stack-auto', '-c', 'test.c', '-o', label + '.rel'],
                    cwd=root, env=env, capture_output=True)
                (root / (label + '.log')).write_bytes(run.stdout + run.stderr)
                assert run.returncode == 0, (root, label, run.stderr)
                variants[label] = instructions((root / (label + '.asm')).read_text('latin1'))
            before, after = variants['parent'], variants['candidate']
            assert 'mov r7,a' in before, (root, 'missing regression trigger')
            expected = [s for s in before if s != 'mov r7,a'] if removable else before
            assert after == expected, (root, before, after)
            rows.append(dict(model=model, optimization=opt, case=name, removed=removable,
                             allOtherInstructionsIdentical=True))
(a.output / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', newline='\n')
print('PASS', len(rows), 'liveness cases; exact remaining instruction streams match')
