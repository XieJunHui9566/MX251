"""Target SFR address classification, ISR body preservation and legacy default."""
import argparse, json, os, re, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'baseline', 'toolchain', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.toolchain.resolve())
env['PATH'] = str(a.toolchain.resolve()/'bin')+';'+env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('size', 'speed'):
        root = a.output/(model+'-'+opt)
        root.mkdir()
        flags = ['-mmcs251', '--model-'+model, '--opt-code-'+opt, '--stack-auto', '--mcs251-wide-ordinary']
        def compile(source, tag, layout=None, compiler=a.sdcc, extra=(), success=True):
            (root/'test.c').write_text(source, newline='\n')
            cmd = [str(compiler.resolve()), *flags]
            if layout: cmd += ['--mcs251-sfr-layout', layout]
            cmd += [*extra, '-c', 'test.c', '-o', tag+'.rel']
            run = subprocess.run(cmd, cwd=root, env=env, capture_output=True)
            (root/(tag+'.log')).write_bytes(run.stdout+run.stderr)
            assert (run.returncode == 0) == success, (tag, run.stderr)
            return (root/(tag+'.asm')).read_text(encoding='latin1') if success else ''
        for address in (0x81, 0x85, 0xbe):
            source = f'__sfr __at({address}) peripheral; void irq(void) __interrupt(1) {{ peripheral=0; peripheral ^= 0x55; }}\n'
            tag = hex(address)
            old = compile(source, tag+'-old', compiler=a.baseline)
            default = compile(source, tag+'-default')
            assert (root/(tag+'-old.rel')).read_bytes() == (root/(tag+'-default.rel')).read_bytes()
            asm = compile(source, tag+'-target', 'sph85-v1')
            assert ('mx251-leaf-isr-saves' in asm) == (address == 0xbe), (tag, asm)
            def body(text):
                text = text.split('mx251-isr-body-begin', 1)[1].split('mx251-isr-body-end', 1)[0]
                return [s for line in text.splitlines() if (s := line.split(';', 1)[0].strip())]
            assert body(old) == body(asm), (tag, 'volatile body changed')
            rows.append(dict(model=model, opt=opt, address=address, optimized=address == 0xbe))
        extra = ['--mcs251-ordered-asm', '--mcs251-ordered-asm-bits']
        compile('void f(void){}', 'layout-invalid', 'unknown', success=False)
        compile('void f(void){}', 'stack-bits', 'sph85-v1', extra=[*extra, '85:01'], success=False)
        compile('void f(void){}', 'legacy-stack-bits', extra=[*extra, 'be:01'], success=False)
        compile('void f(void){}', 'peripheral-bits', 'sph85-v1', extra=[*extra, 'be:01'])
(a.output/'result.json').write_text(json.dumps(dict(status='PASS', addressCases=12, legacyObjectComparisons=12, optionCases=16, rows=rows), indent=2)+'\n')
print('PASS: 12 address cases, 12 legacy object comparisons, 16 option checks')
