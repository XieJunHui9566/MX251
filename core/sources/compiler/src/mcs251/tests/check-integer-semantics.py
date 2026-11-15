"""Check generic word products and signed quotients in both integer ABIs."""
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
source = Path(__file__).with_name('integer-semantics.c').read_text()
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env.update(SDCC_HOME=str(a.toolchain), PATH=str(a.toolchain/'bin')+os.pathsep+env['PATH'],
           MX251_WIDE_VALUE_LIMIT='1024', MX251_WIDE_NODE_LIMIT='2048')
rows = []
for model in ('small', 'large'):
    for opt in ('size', 'speed'):
        for abi in (2, 4):
            root = a.output / f'{model}-{opt}-{abi}'
            root.mkdir(); (root/'probe.c').write_text(source)
            args = [str(a.sdcc), '-mmcs251', '--model-'+model, '--opt-code-'+opt,
                '--std-sdcc11', '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
                '--mcs251-function-metadata', '-DTEST_ABI='+str(abi)]
            if model == 'small': args.append('--mcs251-default-edata')
            if abi == 4: args.append('--mcs251-native-int-runtime')
            run = subprocess.run(args+['-c', 'probe.c'], cwd=root, env=env, capture_output=True)
            (root/'compile.log').write_bytes(run.stdout+run.stderr)
            assert not run.returncode, run.stderr
            asm = (root/'probe.asm').read_text()
            functions = {m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)(.*?)'
                r'; mx251-function end schema=1', asm, re.S)}
            body = lambda name: next(v for k,v in functions.items() if k.endswith('_'+name))
            products = body('product_mix')
            assert 'wide word-origin summary products=3 ' in products, root
            assert not re.search(r'\b(?:ecall|ejmp)\s+\S*mullong', products), root
            quotients = body('quotient_mix')
            shifts = [int(s) for s in re.findall(r'wide signed power2 quotient width=4 shift=(\d+)', quotients)]
            assert shifts == [1, 7, 8, 10, 15, 16, 30], (root, shifts)
            assert not re.search(r'\b(?:ecall|ejmp)\s+\S*divslong', quotients), root
            assert re.search(r'\b(?:ecall|ejmp)\s+\S*mullong', body('product_unknown')), root
            for name in ('divide_unknown', 'remainder_signed', 'divide_negative', 'user_call'):
                assert 'wide signed power2' not in body(name) and 'wide word-origin summary' not in body(name)
            # The two volatile assignments and snapshots remain two separate transactions.
            snapshot = body('product_snapshot')
            assert snapshot.count('wide edata store _sampled width=2') == 2, root
            assert snapshot.count('wide edata load _sampled width=2') == 2, root
            assert re.search(r'wide word-origin summary products=1 narrow-homes=[01]\b', body('product_shared')), root
            rows.append(dict(model=model, optimization=opt, abi=abi, signedShifts=shifts,
                             fullProducts=3, volatileReads=2, volatileWrites=2))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2)+'\n')
print('PASS: 8 configurations; full products, signed shifts, shared values, volatile ordering and ordinary calls')
