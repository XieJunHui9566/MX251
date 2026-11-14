"""Compile architectural SFR effects, access ordering, legacy masks and core exclusions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'baseline', 'support', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']

# Independent expected ABI exclusions: register aliases, stack, mode and
# mapping controls. Test every SFR bit, including both SPH layouts.
core = {0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0xa2, 0xd0, 0xd1, 0xe0, 0xe3, 0xf0}
blocked = {(at, bit) for at in core for bit in range(8)}
blocked |= {(0x87, bit) for bit in range(6)} | {(0x8e, 1), (0xba, 7)}
allowed = sorted({(at, bit) for at in range(128, 256) for bit in range(8)}-blocked)
header = '''typedef unsigned char u8;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(TEST_ABI)
volatile __data u8 cell;
'''
positive = header
expected = {}
for at in sorted({at for at, _ in allowed}):
    name = f'port_{at:02x}'
    positive += f'u32 {name}(u32 x) ABI {{\n'
    ops = []
    for _, bit in [pair for pair in allowed if pair[0] == at]:
        positive += f'cell={bit};\n__asm\n'
        for op in ('setb', 'cpl', 'clr'):
            positive += f'{op} 0x{at:02x}.{bit}\n'
            ops.append((op, f'0x{at:02x}.{bit}'))
        positive += '__endasm;\nx=(x<<3)^cell;\n'
    positive += 'return x;\n}\n'
    expected[name] = ops
negative = header
for at, bit in sorted(blocked | {(0x20, 0), (0x7f, 7)}):
    negative += f'u32 core_{at:02x}_{bit}(u32 x) ABI {{ __asm setb 0x{at:02x}.{bit} __endasm; return x+1; }}\n'

rows = []
for abi in (2, 4):
    for model in ('small', 'large'):
        for opt in ('size', 'speed'):
            d = a.output/f'abi{abi}-{model}-{opt}'
            d.mkdir()
            flags = ['-mmcs251', '--model-'+model, '--opt-code-'+opt, '--stack-auto', '--std-sdcc11',
                     '--mcs251-native-abi', '--mcs251-wide-ordinary', '--mcs251-ordered-asm',
                     '--mcs251-machine-ir', '--mcs251-function-metadata', '--mcs251-sfr-layout', 'sph85-v1',
                     '-DTEST_ABI='+str(abi)]

            def compile(name, source, compiler, bits, extra=(), success=True):
                (d/(name+'.c')).write_text(source)
                args = [str(compiler.resolve()), *flags, '--mcs251-ordered-asm-bits', bits,
                        *extra, '-c', name+'.c']
                result = subprocess.run(args, cwd=d, env=env, capture_output=True)
                (d/(name+'.log')).write_bytes(result.stdout+result.stderr)
                assert (result.returncode == 0) == success, (d, name, result.stderr)
                if success:
                    return (d/(name+'.asm')).read_text(errors='replace'), (d/(name+'.rel')).read_bytes()

            asm, _ = compile('ports', positive, a.sdcc, 'peripheral-v1')
            (d/'ports-peripheral.asm').write_text(asm)
            refusals = re.findall(r'; wide refusal function=(\S+) stage=\S+ reason=(\S+)', asm)
            assert not refusals, (d, refusals)
            selected = {'wide':0, 'legacy':0}
            for name, operations in expected.items():
                symbol = ('_' if abi == 2 else '___m251r4_')+name
                body = asm.split('; mx251-function begin schema=1 symbol='+symbol+' ', 1)[1].split('; mx251-function end', 1)[0]
                actual = re.findall(r'^\s*(setb|cpl|clr)\s+(0x[0-9a-f]{2}\.[0-7])\s*$', body, re.M)
                assert actual == operations, (d, name, actual)
                choice = re.search(r'; mx251-body-choice function='+symbol+r' selected=(\w+)',body)
                assert choice, (d, name, 'backend not compared')
                selected[choice[1]] += 1
                sequence = []
                for op, args in re.findall(r'^\s*(mov|setb|cpl|clr)\s+([^;\n]+)',body,re.M):
                    if op != 'mov':
                        sequence.append(op)
                    elif '_cell' in args:
                        sequence.append('store' if '_cell' in args.split(',')[0] else 'load')
                assert sequence == ['store','setb','cpl','clr','load']*(len(operations)//3), (d, name, sequence)
            for layout in ('legacy-v1', 'sph85-v1'):
                source = negative
                if layout == 'legacy-v1':
                    for bit in range(8):
                        source += f'u32 core_be_{bit}(u32 x) ABI {{ __asm setb 0xbe.{bit} __endasm; return x+1; }}\n'
                asm, obj = compile('core', source, a.sdcc, 'peripheral-v1', ['--mcs251-sfr-layout', layout])
                (d/('core-'+layout+'.asm')).write_text(asm)
                count = len(blocked)+2+(8 if layout == 'legacy-v1' else 0)
                assert len(re.findall(r'; wide refusal function=', asm)) == count, (d, layout)
                _, old = compile('core', source, a.baseline, '88:ff', ['--mcs251-sfr-layout', layout])
                assert obj == old, (d, layout, 'core state output changed')
            # Historical exact masks keep their historical admission and output.
            _, obj = compile('masks', positive, a.sdcc, '88:ff,af:ff')
            _, old = compile('masks', positive, a.baseline, '88:ff,af:ff')
            assert obj == old, (d, 'numeric mask compatibility')
            # Peripheral mode cannot declare a mapping/storage transition implicitly.
            mapping = header+'u32 map(u32 x) ABI { __asm setb 0xba.7 __endasm; return (x>>13)^cell; }\n'
            asm, _ = compile('mapping', mapping, a.sdcc, 'peripheral-v1', ['--mcs251-memory-state-bits','ba:80'])
            assert 'wide-mir accepted: map' in asm and 'memory-state=1' in asm
            rows.append(dict(abi=abi,model=model,optimization=opt,functions=len(expected),bits=len(allowed),
                             operations=len(allowed)*3,selected=selected,orderedAccesses=True,coreFallbackMatches=True,numericMasksMatch=True))
            if len(rows) == 1:
                for i, bad in enumerate(('peripheral', 'peripheral-v1,88:ff', 'PERIPHERAL-V1')):
                    compile('invalid'+str(i),'void f(void) {}',a.sdcc,bad,success=False)
                compile('invalid-memory','void f(void) {}',a.sdcc,'peripheral-v1',
                        ['--mcs251-memory-state-bits','peripheral-v1'],success=False)

(a.output/'results.json').write_text(json.dumps(dict(status='PASS',
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),rows=rows),indent=2)+'\n')
print('PASS',len(rows),'configurations;',len(allowed),'SFR bits;',
      len(blocked)+2,'/',len(blocked)+10,'excluded bit cases for SPH85 / legacy layouts')
