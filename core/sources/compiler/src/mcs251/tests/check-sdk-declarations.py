"""Compile declaration regressions and compare elided/explicit initializer bytes."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--toolchain', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
cc = a.toolchain.resolve()/'bin/sdcc.exe'
flags = ['-mmcs251', '--model-small', '--stack-auto', '--std-sdcc11', '-S']
checks = []


def compile(name, source, success=True):
    root = a.output/name
    root.mkdir()
    (root/'input.c').write_text(source, encoding='utf-8')
    q = subprocess.run([str(cc), *flags, 'input.c'], cwd=root, capture_output=True)
    (root/'compiler.log').write_bytes(q.stdout+q.stderr)
    assert (q.returncode == 0) == success, (name, q.stderr.decode(errors='replace'))
    checks.append(dict(name=name, exitCode=q.returncode))
    return (root/'input.asm').read_text() if success else ''


for name, source in {
    'code-tentative-before': 'char __code desc[3]; char __code desc[3]={1,2,3};',
    'code-tentative-after': 'char __code desc[3]={1,2,3}; char __code desc[3];',
    'code-zero': 'char __code desc[3]; char __code desc[3];',
    'edata-local': 'unsigned long f(unsigned long n) __reentrant { volatile unsigned long __edata v=n; volatile unsigned long *p=&v; return *p+1; }',
    'designated': 'struct S {unsigned char a[2];unsigned char b;}; const struct S table[2]={[1]={.b=3,.a={1,2}}};',
}.items():
    compile(name, source)
compile('duplicate-initializers', 'char __code a=1; char __code a=2;', False)

cases = [
    ('font', 'struct S {unsigned char name[2]; unsigned char data[3];};',
     'const struct S table[]={"ab",1,2,3,"cd",4,5,6};',
     'const struct S table[]={ {"ab",{1,2,3}}, {"cd",{4,5,6}} };', 10),
    ('nested', 'struct S {unsigned char a[2][2]; unsigned char b;};',
     'const struct S table[]={1,2,3,4,5,6,7};',
     'const struct S table[]={ {{{1,2},{3,4}},5}, {{{6,7},{0,0}},0} };', 10),
    ('mixed', 'struct S {unsigned char a[2]; unsigned char b;};',
     'const struct S table[]={ {1,2,3}, { {4,5},6} };',
     'const struct S table[]={ {{1,2},3}, {{4,5},6} };', 6),
    ('union', 'union U {unsigned char a[2]; unsigned int b;};',
     'const union U table[]={1,2,3,4};',
     'const union U table[]={ {{1,2}}, {{3,4}} };', 4),
]
for name, decl, flat, explicit, size in cases:
    def data(asm):
        s = asm.split('_table:', 1)[1]
        return [re.sub(r'\s+', ' ', l.split(';')[0].replace('#', '').strip())
                for l in s.splitlines() if l.strip().startswith(('.db ', '.dw ', '.ascii '))]
    suffix = f'\n_Static_assert(sizeof(table)=={size}, "size");'
    x = compile(name+'-flat', decl+flat+suffix)
    y = compile(name+'-explicit', decl+explicit+suffix)
    assert data(x) == data(y), (name, data(x), data(y))
    assert data(x), name
(a.output/'result.json').write_text(json.dumps(dict(status='PASS', checks=checks), indent=2)+'\n')
print('PASS', len(checks), 'declaration checks')
