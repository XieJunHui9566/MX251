"""Check indexed address selection and its disabled form against the parent compiler."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'baseline', 'support', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env.update(SDCC_HOME=str(a.support.resolve()), MX251_BODY_BACKEND_CHOICE='0',
           MX251_LINEAR_BACKEND_CHOICE='0')
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
extra = '''#include "indexed-address.h"
u8 index_byte(const u8 *p,u8 i) { return p[(u8)(i*2U)]; }
u8 index_eight(const u8 *p,u16 i) { return p[(u16)(i<<3)]; }
void index_constant_store(u8 *p,u16 i) { p[(u16)(i*2U)]=0xa5; }
u8 index_volatile(const volatile u8 *p,u16 i) { return p[i*2U]; }
extern void barrier(void);
u8 index_call(const u8 *p,u16 i) { u16 offset=i*2U; barrier(); return p[offset]; }
'''

def body(text, name):
    return text.split('symbol=_' + name + ' ', 1)[1].split('; mx251-function end', 1)[0]

def instructions(text):
    # Comments contain the compiler version and backend diagnostics.
    return [line.split(';', 1)[0].strip() for line in text.splitlines()
            if line.split(';', 1)[0].strip()]

rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        variants = {}
        for variant, compiler in (('parent', a.baseline), ('enabled', a.sdcc), ('disabled', a.sdcc)):
            d = a.output / (model + '-' + opt) / variant
            d.mkdir(parents=True)
            for name in ('machine-board.h', 'indexed-address.h', 'indexed-address-access.c'):
                shutil.copy2(tests / name, d / name)
            (d / 'extra.c').write_text(extra)
            runenv = dict(env, MX251_INDEXED_ADDRESS='0' if variant == 'disabled' else '1')
            variants[variant] = {}
            for source in ('indexed-address-access', 'extra'):
                cmd = [str(compiler.resolve()), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
                       '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
                       '--mcs251-flat-code-reads', '--mcs251-function-metadata', '-c', source + '.c']
                q = subprocess.run(cmd, cwd=d, env=runenv, capture_output=True)
                (d / (source + '.log')).write_bytes(q.stdout + q.stderr)
                assert q.returncode == 0, q.stderr
                rows.append(dict(command=cmd, directory=str(d), exitCode=q.returncode))
                variants[variant][source] = (d / (source + '.asm')).read_text()
        for source in variants['parent']:
            assert instructions(variants['parent'][source]) == instructions(variants['disabled'][source]), (model, opt, source)
        enabled = variants['enabled']['indexed-address-access']
        parent = variants['parent']['indexed-address-access']
        for name in ('index_twice', 'index_four', 'index_code_byte', 'index_code_word'):
            assert 'wide indexed scale fused' in body(enabled, name), (model, opt, name)
        assert 'wide indexed offset-first' in body(enabled, 'index_write')
        assert 'wide indexed scale fused' in body(variants['enabled']['extra'], 'index_constant_store')
        assert 'wide indexed scale fused' not in body(enabled, 'index_reused')
        for name in ('index_code_byte', 'index_code_word'):
            assert len(re.findall(r'\bmovc\b', body(enabled, name))) == len(re.findall(r'\bmovc\b', body(parent, name)))
        for name in ('index_byte', 'index_eight', 'index_volatile', 'index_call'):
            assert 'wide indexed scale fused' not in body(variants['enabled']['extra'], name), name
result = dict(status='PASS', configurations=4, commands=rows,
              compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
              parentCompilerSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest())
(a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: four configurations, parent/disabled equality, RAM/CODE/store fusion, integer-width and reuse controls')
