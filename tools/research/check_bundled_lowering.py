"""Check both backends and private-bit feedback, including the old contract."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from bundled_values_fixture import SOURCE

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'support', 'output'): p.add_argument('--'+key, type=Path, required=True)
a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
rows = []
for model in ('small', 'large'):
    for objective in ('speed', 'size'):
        root = a.output/(model+'-'+objective); root.mkdir()
        flags = ['-mmcs251', '--model-'+model, '--opt-code-'+objective, '--stack-auto',
                 '--mcs251-function-metadata', '--mcs251-machine-ir']
        def compile(tag, source, extra=(), extra_env=None):
            (root/(tag+'.c')).write_text(source)
            command = [str(a.sdcc.resolve()), *flags, *extra, '-c', tag+'.c', '-o', tag+'.rel']
            q = subprocess.run(command, cwd=root, env=dict(env, **(extra_env or {})), capture_output=True, timeout=60)
            (root/(tag+'.log')).write_bytes(q.stdout+q.stderr)
            assert q.returncode == 0, (root, tag, q.stderr)
            return (root/(tag+'.asm')).read_text(encoding='latin1')
        for backend in ('legacy', 'wide'):
            source = '#define FN(name) probe_##name\n#define ABI '+('__sdcccall(4)' if backend == 'wide' else '')+'\n'+SOURCE
            asm = compile(backend, source, ['--mcs251-wide-ordinary'] if backend == 'wide' else [])
            body = re.search(r'; mx251-function begin [^\n]*symbol=\S*probe_sample isr=.*?; mx251-function end',asm,re.S)[0]
            # Two volatile samples remain after the preceding store.
            if backend == 'wide':
                if 'wide-mir accepted: probe_sample;' in body:
                    assert body.count('wide symbol load _probe_observed') == 2, (root, body)
                else:
                    assert len(re.findall(r'^\s*mov\s+a,@dpx\s*$',body,re.M)) == 4, (root, body)
            assert '_probe_mutate' in asm and '_probe_store_alias' in asm
            rows.append(dict(model=model, objective=objective, backend=backend))
        source = 'void helper(void); void irq(void) __interrupt(1) { helper(); }\n'
        for version, bits in [(3,1), (4,0), (4,1)]:
            (root/'effects.txt').write_text(f'MX251-CALL-CLOBBERS-{version}\nglobal _helper 2 00000c00 0'+(f' {bits}' if version==4 else '')+'\n')
            asm = compile(f'isr-v{version}-{bits}', source, ['--mcs251-wide-ordinary', '--mcs251-leaf-effects', 'effects.txt'])
            assert bool(re.search(r'^\s*push\s+bits\s*$',asm,re.M)) == bool(bits), (root, version, bits)
            assert bool(re.search(r'^\s*pop\s+bits\s*$',asm,re.M)) == bool(bits)
            if not bits: assert 'mx251-isr-bits-call callee=_helper omitted=1' in asm
        (root/'effects.txt').write_text('MX251-CALL-CLOBBERS-4\nglobal _helper 2 00000c00 0 0\n')
        asm = compile('isr-optout', source, ['--mcs251-wide-ordinary', '--mcs251-leaf-effects', 'effects.txt'], {'MX251_BITS_CALL_EFFECTS':'0'})
        assert re.search(r'^\s*push\s+bits\s*$',asm,re.M)
report = dict(status='PASS', compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
              backendConfigurations=rows, privateBitConfigurations=16,
              note='Numerical results are checked on the board by bundled_values_fixture.')
(a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 8 value configurations and 16 private-bit feedback configurations')
