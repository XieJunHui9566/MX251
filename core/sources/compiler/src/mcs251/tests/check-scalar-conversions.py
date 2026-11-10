"""Integer call facts and parameter specialization across both memory models."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(args.support.resolve())
env['PATH'] = str(args.support.resolve() / 'bin') + os.pathsep + env['PATH']
expected = dict(sc_wake=['000007d0'], sc_u16=['000007d0', '?'],
    sc_i16=['0000f830', '?'], sc_u32=['ffffffff', '?'],
    sc_i32=['80000001', '?'], sc_u8=['00000034', '?'], sc_i8=['000000ff', '?'],
    sc_stack=['?', '00002345', '0000fff9', 'ffffffff'], sc_change=['0000fffa', '?'],
    sc_prune=['00000000', '?'])
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        directory = args.output / (model + '-' + opt)
        directory.mkdir()
        for path in tests.glob('scalar-conversions*'):
            shutil.copy2(path, directory / path.name)
        command = [str(args.sdcc.resolve()), '-mmcs251', '--model-' + model,
            '--opt-code-' + opt, '--stack-auto', '--mcs251-wide-ordinary',
            '--mcs251-machine-ir', '--mcs251-function-metadata']
        def compile(source, extra=()):
            result = subprocess.run(command + list(extra) + ['-c', source + '.c'],
                                    cwd=directory, env=env, capture_output=True)
            (directory / (source + '.log')).write_bytes(result.stdout + result.stderr)
            assert not result.returncode, result.stderr
            return (directory / (source + '.asm')).read_text(), result.stderr.decode()
        assembly, _ = compile('scalar-conversions-callers')
        calls = re.findall(r'mx251-scalar-call schema=1 owner=\S+ target=_(\S+) signature=(\S+) values=(\S+)', assembly)
        facts, seen = [], set()
        for name, signature, values in calls:
            values = values.split(',')
            if name not in expected:
                continue
            assert values == expected[name], (name, values, expected[name])
            if name not in seen:
                facts += [f'_{name}\t{signature}\t{i}\t{value}'
                          for i, value in enumerate(values, 1) if value != '?']
            seen.add(name)
        assert seen == set(expected)
        assert [v for n, s, v in calls if n == 'sc_i16'] == ['0000f830,?'] * 2
        assert set(v for n, s, v in calls if n == 'sc_multi') == {'000007d0,?', '000007d1,?'}
        assert [v for n, s, v in calls if n == 'sc_unknown'] == ['?']
        (directory / 'facts.txt').write_text('MX251-ORDINARY-SCALAR-V1\n' + '\n'.join(facts) + '\n')
        specialized, diagnostics = compile('scalar-conversions-callees', ['--mcs251-scalar-parameters', 'facts.txt'])
        assert diagnostics.count('MX251 ordinary scalar applied ') == len(facts)
        for name in expected:
            body = specialized.split('symbol=_' + name + ' ', 1)[1].split('; mx251-function end', 1)[0]
            assert not re.search(r'\b(?:ecall|ejmp)\s+\S*(?:div|mod)', body), name
        rows.append(dict(model=model, optimization=opt, facts=len(facts),
                         convertedParameters=True, dynamicAndDifferentValuesRetained=True))
(args.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2) + '\n')
print('PASS: 4 configurations; signed/unsigned 8/16/32-bit and stacked constants; dynamic and differing arguments')
