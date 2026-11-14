"""Check range-proven native division and unchanged signed boundaries."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdcc', type=Path, required=True)
p.add_argument('--toolchain', type=Path, required=True)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = Path(__file__).with_name('range-native-divmod.c').read_text()
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.toolchain)
env['PATH'] = str(a.toolchain / 'bin')+';'+env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('size', 'speed'):
        for abi in (2, 4):
            root = a.output / f'{model}-{opt}-{abi}'
            root.mkdir()
            args = ['-mmcs251', '--model-'+model, '--opt-code-'+opt, '--std-sdcc11', '--stack-auto',
                    '--mcs251-wide-ordinary', '--mcs251-machine-ir', '--mcs251-function-metadata',
                    '-DTEST_ABI='+str(abi), '-c', 'probe.c']
            variants = {}
            for name, compiler in [('before', a.baseline), ('after', a.sdcc)]:
                out = root / name
                out.mkdir()
                (out / 'probe.c').write_text(source)
                run = subprocess.run([str(compiler), *args], cwd=out, env=env, capture_output=True)
                (out / 'compile.log').write_bytes(run.stdout+run.stderr)
                assert not run.returncode, run.stderr
                asm = (out / 'probe.asm').read_text()
                functions = {}
                for match in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
                                         r'; mx251-function end schema=1', asm, re.S):
                    functions[match[1]] = match[2]
                variants[name] = functions
            transformed = sum(text.count('; wide runtime-native-division ') for text in variants['after'].values())
            native = {}
            for name in ('promoted_mod','promoted_div','variable_div','variable_mod','bounded_div','paired','volatile_mod'):
                text = next(v for k,v in variants['after'].items() if k.endswith('_'+name))
                assert 'wide-mir accepted: '+name in text, (root,name)
                assert not re.search(r'\b(?:ecall|ejmp)\s+__(?:mod|div)', text), (root,name)
                native[name] = len(re.findall(r'^\s*div\s+(?:ab|wr)',text,re.M))
                assert native[name] == (2 if name=='volatile_mod' else 1), (root,name,native[name])
            for name in ('negative_mod','negative_div','wrapped_mod','negative_divisor','unknown_divisor','wide_div'):
                old = next(v for k,v in variants['before'].items() if k.endswith('_'+name))
                new = next(v for k,v in variants['after'].items() if k.endswith('_'+name))
                # Ignore only one-time legacy register alias declarations.
                code = lambda s: '\n'.join(l for l in s.splitlines() if l and not l.startswith(';') and not re.match(r'ar[0-7]\s*=',l.strip()))
                assert code(old) == code(new), (model,opt,abi,name)
            text = next(v for k,v in variants['after'].items() if k.endswith('_volatile_mod'))
            assert len(re.findall(r'^\s*mov\s+[^,]+,e:\(_sampled\+0\)',text,re.M)) == 2, root
            rows.append(dict(model=model,optimization=opt,abi=abi,runtimeTransformations=transformed,
                nativeInstructions=native,negativeBoundaryCodeUnchanged=True,volatileReads=2))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2))
print('PASS: eight configurations; native division, signed boundaries and distinct volatile reads')
