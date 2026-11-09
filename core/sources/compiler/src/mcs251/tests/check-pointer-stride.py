"""Keep byte-load result types out of pointer index arithmetic."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

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
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('machine-board.h', 'pointer-stride.h', 'pointer-stride-access.c'):
            shutil.copy2(tests / name, d / name)
        cmd = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
               '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
               '--dump-i-code', '-c', 'pointer-stride-access.c']
        q = subprocess.run(cmd, cwd=d, capture_output=True, env=env)
        (d / 'compile.log').write_bytes(q.stdout + q.stderr)
        assert q.returncode == 0, q.stderr
        raw = (d / 'pointer-stride-access.dumpraw0').read_text()
        for name in ('signed', 'unsigned', 'nested', 'subscript', 'volatile', 'word', 'store'):
            body = raw.split('proc _stride_' + name + ' ', 1)[1].split('eproc ', 1)[0]
            arithmetic = [line for line in body.splitlines() if ' << ' in line or ' * ' in line]
            assert arithmetic, name
            for line in arithmetic:
                result_type = re.findall(r'\{([^{}]+)\}', line.split(' = ', 1)[0])[-1]
                assert 'char' not in result_type, (name, result_type, line)
        # An explicit source cast must still retain modulo-256 behavior.
        body = raw.split('proc _stride_wrapped ', 1)[1].split('eproc ', 1)[0]
        assert '(unsigned-char fixed)' in body or re.search(r'\{unsigned-char fixed\} = .* << ', body)
        rows.append(dict(command=cmd, directory=str(d), exitCode=q.returncode))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=4, commands=rows), indent=2) + '\n')
print('PASS: small/large x speed/size; signed, unsigned, nested, subscript, volatile, word, store, and explicit byte cast')
