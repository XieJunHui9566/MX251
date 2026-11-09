"""Check native representations of cast-only 24-bit pointer offsets."""
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
extra = '''#include "pointer-stride.h"
long scalar_cast(long i) { return (_BitInt(24))i; }
u8 compared_offset(const u8 *p,long i) { _BitInt(24) o=i; return *(p+o) + (o<0); }
u8 stored_offset(const u8 *p,long i) { volatile _BitInt(24) o=i; return *(p+o); }
u8 mutate_source(const u8 *p,int i) { _BitInt(24) o=i; ++i; return *(p+o)^i; }
u8 mixed_signs(const u8 *p,unsigned int a,int b,u8 choose) {
    _BitInt(24) o; if(choose)o=a;else o=b;return *(p+o);
}
'''
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('machine-board.h', 'pointer-stride.h', 'pointer-stride-access.c'):
            shutil.copy2(tests / name, d / name)
        (d / 'extra.c').write_text(extra)
        for source in ('pointer-stride-access', 'extra'):
            cmd = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
                   '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
                   '--mcs251-function-metadata', '-c', source + '.c']
            q = subprocess.run(cmd, cwd=d, env=env, capture_output=True)
            (d / (source + '.log')).write_bytes(q.stdout + q.stderr)
            assert q.returncode == 0, q.stderr
            rows.append(dict(command=cmd, directory=str(d), exitCode=q.returncode))
        asm = (d / 'pointer-stride-access.asm').read_text()
        for name in ('signed', 'cast_signed', 'cast_unsigned', 'cast_subtract', 'cast_pair',
                     'merge', 'byte', 'code', 'volatile', 'word', 'store'):
            assert 'wide-mir accepted: stride_' + name + ';' in asm, name
        for width in (8, 16, 32):
            assert re.search(r'wide address-only offset .*native=' + str(width) + r'\b', asm), width
        code = asm.split('symbol=_stride_code ', 1)[1].split('; mx251-function end', 1)[0]
        assert 'movc' in code and 'wide code load' in code
        asm = (d / 'extra.asm').read_text()
        for name in ('scalar_cast', 'compared_offset', 'stored_offset'):
            assert 'wide-mir accepted: ' + name not in asm, name
        assert 'wide-mir accepted: mutate_source;' in asm
        mixed = asm.split('symbol=_mixed_signs ', 1)[1].split('; mx251-function end', 1)[0]
        assert re.search(r'wide address-only offset .*native=32 sign=0 definitions=2', mixed)
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=4, commands=rows), indent=2) + '\n')
print('PASS: small/large x speed/size; 8/16/32-bit offsets, mixed definitions, CODE, and ordinary 24-bit uses')
