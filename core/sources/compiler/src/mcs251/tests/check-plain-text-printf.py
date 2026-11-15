"""Check MCS251 printf-to-puts lowering, literal contents, ABIs and exclusions."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'toolchain', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env.update(SDCC_HOME=str(a.toolchain.resolve()), PATH=str(a.toolchain.resolve()/'bin')+os.pathsep+env['PATH'])
source = Path(__file__).with_name('plain-text-printf.c').read_text()
changed = dict(plain=b'hello\0', newline=b'\0', multiline=b'one\ntwo\0',
               crlf=b'line\r\0', shared_unused=b'shared\0')
unchanged = ['shared_used', 'no_newline', 'escaped_percent', 'formatted',
             'variable', 'embedded_zero', 'extra_argument']
rows = []


def compile_case(name, text, options):
    root = a.output/name
    root.mkdir()
    (root/'test.c').write_text(text, encoding='utf-8', newline='\n')
    cmd = [str(a.sdcc.resolve()), '-mmcs251', '--opt-code-size',
           '--mcs251-wide-ordinary', '--mcs251-machine-ir', *options, '-c', 'test.c']
    q = subprocess.run(cmd, cwd=root, env=env, capture_output=True)
    (root/'compile.log').write_bytes(q.stdout+q.stderr)
    assert q.returncode == 0, (name, q.returncode, q.stderr)
    asm = (root/'test.asm').read_text(encoding='latin1')
    assert (root/'test.rel').is_file()
    rows.append(dict(name=name, status='PASS'))
    return asm


def body(asm, name):
    return re.search(r'^_(?:__m251r4_)?'+name+r':\s*\n(.*?)(?=^_(?:__m251r4_)?[A-Za-z]|^\s*\.area|\Z)',
                     asm, re.M | re.S)[1]


for model in ('small', 'large'):
    for abi in ('ordinary', 'native'):
        options = ['--model-'+model, '--stack-auto', '--mcs251-native-abi']
        if abi == 'native':
            options += ['-DTEST_ABI=__reentrant __sdcccall(4)']
        for disabled in (False, True):
            name = model+'-'+abi+('-disabled' if disabled else '')
            asm = compile_case(name, source, options+(['--nostdlibcall'] if disabled else []))
            literals = dict(re.findall(r'mx251-string-literal schema=1 symbol=(\S+) bytes=([0-9a-f]+)', asm))
            for fn, expected in changed.items():
                code = body(asm, fn)
                api = 'printf' if disabled else 'puts'
                assert re.search(r'\b(?:e|l|a)(?:call|jmp)\s+_'+api+r'\b', code), (name, fn)
                if not disabled:
                    symbols = set(re.findall(r'___str_\w+', code))
                    assert len(symbols) == 1, (name, fn, symbols)
                    assert bytes.fromhex(literals[symbols.pop()]) == expected, (name, fn)
                    assert not re.search(r'^\s*pushw?\b', code, re.M), (name, fn)
            for fn in unchanged:
                code = body(asm, fn)
                assert re.search(r'\b(?:e|l|a)(?:call|jmp)\s+_printf\b', code), (name, fn)
            assert re.search(r'\b(?:e|l|a)call\s+_effect\b', body(asm, 'extra_argument'))
            # The literal shared by a transformed and a retained call keeps its newline.
            assert b'shared\n\0' in [bytes.fromhex(v) for v in literals.values()]

for name, declarations, options in [
    ('no-stack-auto', '#include <stdio.h>', []),
    ('no-puts', 'extern int printf(const char *, ...);', ['--stack-auto']),
    ('nonstandard-puts', 'extern int printf(const char *, ...); extern void puts(const char *);', ['--stack-auto']),
    ('nonstandard-printf', 'extern void printf(const char *, ...); extern int puts(const char *);', ['--stack-auto']),
    ('nonvariadic-printf', 'extern int printf(const char *); extern int puts(const char *);', ['--stack-auto']),
    ('code-pointer-puts', 'extern int printf(const char *, ...); extern int puts(const __code char *);', ['--stack-auto']),
]:
    asm = compile_case(name, declarations+'\nvoid f(void) {printf("keep\\n");}\n', ['--model-small', *options])
    assert re.search(r'\b(?:e|l|a)(?:call|jmp)\s+_printf\b', body(asm, 'f')), name

result = dict(status='PASS', compiles=rows, hardwareExecuted=False)
(a.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8', newline='\n')
print('PASS', len(rows), 'compiles: literal contents, return use, side effects, ABI, declarations and opt-out')
