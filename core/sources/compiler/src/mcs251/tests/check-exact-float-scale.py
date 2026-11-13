"""Check exact binary32 scaling elimination and its rounding boundaries."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess


def binary32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def check(compiler, output):
    output.mkdir(parents=True, exist_ok=False)
    prefix = 'typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\n'
    cases = {
        'five_eighths': ('u16 test(u16 x) { return x * 0.625f; }', False),
        'commuted': ('u16 test(u16 x) { return 0.625f * x; }', False),
        'byte': ('u8 test(u8 x) { return x * 0.625f; }', False),
        'power_division': ('u16 test(u16 x) { return (float)x / 8; }', False),
        'signed_result': ('int test(u16 x) { return x * 0.375f; }', False),
        'wide_result': ('u32 test(u16 x) { return x * 3.0f; }', False),
        'tiny': ('u16 test(u16 x) { return x * 0x1p-31f; }', False),
        'volatile_input': ('volatile u16 input; u16 test(void) { return input++ * 0.625f; }', False),
        'signed_input': ('int test(int x) { return x * 0.625f; }', True),
        'rounded_product': ('u16 test(u16 x) { return x * 0.2529296875f; }', True),
        'decimal': ('u16 test(u16 x) { return x * 0.1f; }', True),
        'decimal_division': ('u16 test(u16 x) { return (float)x / 10; }', True),
        'narrow_result': ('u8 test(u16 x) { return x * 0.625f; }', True),
        'signed_overflow': ('int test(u16 x) { return x * 0.625f; }', True),
        'shared_float': ('float saved; u16 test(u16 x) { float y=x*0.625f; saved=y; return y; }', True),
        'volatile_float': ('volatile float saved; u16 test(u16 x) { saved=x*0.625f; return saved; }', True),
    }
    rows = []
    for backend in ('legacy', 'wide'):
        for name, (body, expect_float) in cases.items():
            directory = output / backend / name
            directory.mkdir(parents=True)
            source = directory / 'case.c'
            source.write_text(prefix + body + '\n')
            cmd = [str(compiler), '-mmcs251', '--model-small', '--stack-auto', '--std-sdcc11', '-S', str(source)]
            if backend == 'wide':
                cmd += ['--mcs251-wide-ordinary', '--mcs251-native-int-runtime']
            result = subprocess.run(cmd, cwd=directory, capture_output=True)
            (directory/'compiler.log').write_bytes(result.stdout + result.stderr)
            assert result.returncode == 0, (backend, name, result.stderr.decode(errors='replace'))
            asm = (directory/'case.asm').read_text()
            calls = re.findall(r'^\s*(?:e?l?call|ejmp|ljmp)\s+(___\w*fs\w*)', asm, re.M)
            assert bool(calls) == expect_float, (backend, name, calls)
            rows.append(dict(backend=backend, name=name, floatCalls=calls))
    # Exhaustive domain comparison against independently rounded binary32.
    count = 0
    for numerator, shift in ((5,3), (3,3), (1,3), (3,0), (255,8), (1,31)):
        factor = binary32(numerator / 2**shift)
        for x in range(65536):
            expected = int(binary32(binary32(x) * factor))
            assert expected == x*numerator >> shift, (x, numerator, shift)
            count += 1
    # This factor exceeds the exact-product proof and really does round across
    # an integer boundary, so accepting all binary fractions would be wrong.
    mismatches = [x for x in range(65536)
                  if int(binary32(binary32(x) * binary32(259/1024))) != x*259 >> 10]
    assert mismatches
    report = dict(status='PASS', compilerCases=rows, exhaustiveComparisons=count,
                  roundingCounterexample=dict(input=mismatches[0], factor=259/1024))
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status='PASS', compilerCases=len(rows), exhaustiveComparisons=count,
                          roundingCounterexample=report['roundingCounterexample'])))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check(args.compiler.resolve(), args.output.resolve())
