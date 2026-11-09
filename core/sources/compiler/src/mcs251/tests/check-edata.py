#!/usr/bin/env python3
"""Compile/link EDATA placement and boundary checks; does not claim execution."""
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--toolchain', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--baseline', type=Path, help='optional previous toolchain')
a = p.parse_args()
t = a.toolchain.resolve()
out = a.output_dir.resolve()
out.mkdir(parents=True, exist_ok=False)
rows = []
flags = ['-mmcs251', '--model-small', '--stack-auto', '--std-sdcc11',
         '--opt-code-speed', '--nostdinc', '--nostdlib']
layout = ['--code-loc', '0xff0000', '--xram-loc', '0x10000',
          '--xram-size', '0x7000', '--iram-size', '256']
stack = ['--mcs251-stack', '0x400,0x400,0x800']
libnames = ['mcs251', 'libsdcc', 'libint', 'liblong', 'liblonglong']


def run(d, label, args, ok=True, contains=None):
    q = subprocess.run(list(map(str, args)), cwd=d, capture_output=True, timeout=60)
    log = q.stdout + q.stderr
    (d / (label + '.log')).write_bytes(log)
    assert (q.returncode == 0) == ok, (d.name, label, q.returncode, log)
    if contains:
        assert contains.encode() in log, (d.name, label, log)
    rows.append(dict(case=d.name, step=label, exitCode=q.returncode,
                     expectedSuccess=ok))


def compile(d, name, extra=(), toolchain=t, ok=True):
    run(d, name, [toolchain / 'bin/sdcc.exe', *flags, *extra, '-c', name + '.c'], ok)


def link(d, objects, extra=(), use_stack=True, toolchain=t, ok=True, contains=None):
    libs = [toolchain / 'lib/mcs251-small-stack-auto' / (n + '.lib') for n in libnames]
    run(d, 'link', [toolchain / 'bin/sdcc.exe', *flags, *layout,
                   *(stack if use_stack else []), *extra, *objects, *libs,
                   '-o', 'firmware.hex'], ok, contains)


def source_case(name, source):
    d = out / name
    d.mkdir()
    (d / 'main.c').write_text(source, encoding='utf-8')
    return d


def symbol_address(map_text, name):
    m = re.search(r'^[A-Z]:\s+([0-9A-F]+)\s+' + re.escape(name) + r'\s', map_text, re.M)
    assert m, name
    return int(m[1], 16)


for backend in ('legacy', 'ordinary-wide'):
    d = out / backend
    d.mkdir()
    for n in ('edata', 'edata-other'):
        shutil.copy2(Path(__file__).with_name(n + '.c'), d / (n + '.c'))
        compile(d, n, ['--mcs251-wide-ordinary'] if backend == 'ordinary-wide' else [])
    link(d, ['edata.rel', 'edata-other.rel'])
    m = (d / 'firmware.map').read_text()
    assert symbol_address(m, '_buffer') == 0x100
    assert symbol_address(m, '_word') == 0x204
    assert 0x100 <= symbol_address(m, '_other') < 0x400
    assert symbol_address(m, '_fixed') == 0x300
    assert symbol_address(m, '_absolute_uninitialized') == 0x310
    assert symbol_address(m, '_result') == 0x10000
    assert symbol_address(m, 's_SSEG') == 0x400
    asm = (d / 'edata.asm').read_text()
    init = asm.split('; global & static initialisations')[1].split('; Home')[0]
    for name in ('_buffer', '_word', '_dword', '_item', '_initial', '_saved',
                 '_fixed', '_static_value_local_zero_', '_static_value_local_init_'):
        assert name in init, (backend, 'missing initialization', name)
    # Global aggregate initialization deliberately omits implicit zeros, so
    # require complete-object clearing BEFORE the explicit initializer section.
    for name, size in (('_buffer', 260), ('_partial', 5),
                       ('_static_value_local_abs_10000_3', 4)):
        assert re.search(r'\.area GSINIT3 \(CODE\)\s+; EDATA clear ' + name +
                         r': ' + str(size) + r' bytes[^\n]*\n\s+mov\s+dptr,#' + name +
                         r'\s+mov\s+dpxl,#0\s+mov\s+dr12,#' + str(size) +
                         r'\s+clr\s+a', init), (backend, name)
    assert symbol_address(m, 's_GSINIT3') < symbol_address(m, 's_GSINIT')
    assert 'EXTERNAL RAM     0x10000' in (d / 'firmware.mem').read_text()

for name, source, ok in (
    ('exact-fit', '__edata unsigned char bytes[768];', True),
    ('overflow', '__edata unsigned char bytes[769];', False),
    ('absolute-low', '__edata __at(0xff) unsigned char byte;', False),
    ('absolute-stack', '__edata __at(0x400) unsigned char byte;', False),
    ('absolute-cross-stack', '__edata __at(0x3ff) unsigned int word;', False),
    ('absolute-high', '__edata __at(0x10000) unsigned char byte;', False),
    ('absolute-init-stack', '__edata __at(0x400) unsigned char byte = 1;', False),
):
    d = source_case(name, source + '\nvoid main(void) { for (;;) {} }\n')
    compile(d, 'main')
    link(d, ['main.rel'], ok=ok, contains=None if ok else 'MCS251 EDATA')

d = source_case('no-stack-contract', '__edata unsigned char byte; void main(void) {}')
compile(d, 'main')
link(d, ['main.rel'], use_stack=False, ok=False, contains='MCS251 EDATA')

d = source_case('combined-overflow', '__edata unsigned char a[400]; void main(void) {}')
(d / 'other.c').write_text('__edata unsigned char b[400];')
compile(d, 'main')
compile(d, 'other')
link(d, ['main.rel', 'other.rel'], ok=False, contains='MCS251 EDATA')

d = source_case('absolute-reservation', '__edata __at(0x100) unsigned char fixed[16];\n'
                '__edata unsigned char placed[16]; void main(void) {}')
compile(d, 'main')
link(d, ['main.rel'])
m = (d / 'firmware.map').read_text()
assert symbol_address(m, '_fixed') == 0x100
assert symbol_address(m, '_placed') == 0x110

d = source_case('mcs51-reject', '__edata unsigned char byte;')
run(d, 'compile', [t / 'bin/sdcc.exe', '-mmcs51', '--nostdinc', '-S', 'main.c'], ok=False)
d = source_case('automatic-reject', 'void f(void) { __edata volatile unsigned char x; x=1; }')
compile(d, 'main', ok=False)

if a.baseline:
    baseline = a.baseline.resolve()
    d = source_case('baseline-missing', '__edata unsigned char byte;')
    compile(d, 'main', toolchain=baseline, ok=False)
    d = out / 'old-linker-reject'
    d.mkdir()
    shutil.copy2(out / 'legacy/edata.rel', d / 'edata.rel')
    shutil.copy2(out / 'legacy/edata-other.rel', d / 'edata-other.rel')
    link(d, ['edata.rel', 'edata-other.rel'], toolchain=baseline, ok=False,
         contains='__mcs251_edata_v1')
    for name, tc in (('old', baseline), ('new', t)):
        d = source_case('unchanged-xdata-' + name,
                        'volatile __xdata unsigned int x = 0x1234;\n'
                        'void main(void) { x += 7; for (;;) {} }\n')
        compile(d, 'main', toolchain=tc)
        link(d, ['main.rel'], toolchain=tc)
    assert (out / 'unchanged-xdata-old/firmware.hex').read_bytes() == \
           (out / 'unchanged-xdata-new/firmware.hex').read_bytes()

(out / 'results.json').write_text(json.dumps(dict(checks=rows,
    hardwareExecuted=False, simulated=False), indent=2), encoding='utf-8')
print('PASS:', len(rows), 'compile/link checks; execution not tested')
