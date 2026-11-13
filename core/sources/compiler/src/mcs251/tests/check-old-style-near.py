"""Compile real declarations and check that only near-call metadata expands."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
shutil.copy2(Path(__file__).with_name('old-style-near-cases.c'), a.output/'definitions.c')
(a.output/'calls.c').write_text('''typedef unsigned int u16;
void old_step();
u16 old_read();
u16 complete(void);
u16 with_argument(u16);
u16 caller(u16 x) {
    old_step();
    return old_read() + complete() + with_argument(x);
}
''', newline='\n')
results = []
for unit in ('definitions', 'calls'):
    command = [str(a.compiler.resolve()), '-mmcs251', '--model-small', '--stack-auto',
               '--std-sdcc11', '--mcs251-wide-ordinary', '--mcs251-function-metadata',
               '-S', unit+'.c', '-o', unit+'.asm']
    r = subprocess.run(command, cwd=a.output, capture_output=True)
    (a.output/(unit+'.log')).write_bytes(r.stdout+r.stderr)
    assert r.returncode == 0, (unit, r.stderr.decode(errors='replace'))
    text = (a.output/(unit+'.asm')).read_text()
    if unit == 'definitions':
        for name, signature in [('old_step','v()'), ('old_read','u16()'), ('old_recursive','u16()'),
                                ('old_native','u32()'), ('old_byte','u8()'), ('old_addressed','u16()')]:
            assert re.search(r'mx251-near-definition schema=1 symbol=\S*'+name+r' signature='+re.escape(signature), text), name
            assert not re.search(r'mx251-scalar-definition schema=1 symbol=\S*'+name+r' ', text), name
        assert not re.search(r'mx251-(?:scalar|near)-definition schema=1 symbol=_old_parameter ', text)
        assert 'mx251-near-call schema=1 owner=_old_recursive target=_old_recursive signature=u16()' in text
        checks = 14
    else:
        assert 'mx251-near-call schema=1 owner=_caller target=_old_step signature=v()' in text
        assert 'mx251-near-call schema=1 owner=_caller target=_old_read signature=u16()' in text
        assert 'mx251-scalar-call schema=1 owner=_caller target=_complete signature=u16()' in text
        assert 'mx251-scalar-call schema=1 owner=_caller target=_with_argument signature=u16(u16)' in text
        checks = 4
    results.append(dict(unit=unit, checks=checks, command=command, status='PASS'))
record = dict(status='PASS', checks=sum(r['checks'] for r in results), results=results,
              compilerSha256=hashlib.sha256(a.compiler.read_bytes()).hexdigest())
(a.output/'result.json').write_text(json.dumps(record, indent=2)+'\n', newline='\n')
print('PASS', record['checks'], 'compiled metadata checks')
