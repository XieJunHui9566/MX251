"""Execute native arithmetic assembly against host integer arithmetic."""
import argparse
import importlib.util
import json
from pathlib import Path
import random


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('model', Path(__file__).with_name('machine-ir-model.py'))
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    bodies = {}
    for name in ('mullong', 'divulong'):
        text = (args.source / ('_' + name + '.asm')).read_text(encoding='utf-8')
        bodies[name] = '\n'.join(line for line in text.splitlines() if not line.startswith('.'))
    rng = random.Random(251)
    edges = [0, 1, 2, 3, 7, 127, 128, 255, 256, 257, 32767, 32768, 65535,
             65536, 65537, 0xffffff, 0x1000000, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]
    pairs = [(a, b) for a in edges for b in edges]
    # Exercise every byte divisor, and each wider divisor path.
    pairs += [(rng.getrandbits(32), b) for b in range(1, 256) for _ in range(8)]
    pairs += [(rng.getrandbits(32), rng.getrandbits(width) or 1)
              for width in (8, 16, 24, 32) for _ in range(1024)]
    checks = 0
    for seed, (a, b) in enumerate(pairs):
        prefix = f'_test:\n mov dr4,#{a}\n mov dr12,#{b}\n'
        before_result = model.execute(prefix + ' eret\n', '_test', seed)
        before = before_result[0]
        for name, mask in (('mullong', 0x30ff), ('divulong', 0xff4ff)):
            result = model.execute(prefix + bodies[name], '_test', seed)
            registers, _, sp, events, _, _ = result
            actual = int.from_bytes(registers[4:8], 'big')
            expected = (a * b) & 0xffffffff if name == 'mullong' else a // b if b else 0xffffffff
            assert actual == expected, (name, hex(a), hex(b), hex(actual), hex(expected))
            if name == 'divulong':
                remainder = int.from_bytes(registers[12:16], 'big')
                assert remainder == (a % b if b else a), (name, a, b, remainder)
            assert sp == before_result[2] and not events
            assert all(registers[i] == before[i] for i in range(32) if not (mask >> i) & 1), (name, a, b, 'register preservation')
            checks += 1
    report = dict(status='PASS', operandPairs=len(pairs), executions=checks,
                  properties=['low 32-bit product', 'quotient and remainder', 'preserved registers', 'no stack or memory effects'])
    args.output.write_bytes((json.dumps(report, indent=2) + '\n').encode())
    print(json.dumps(report))


if __name__ == '__main__':
    main()
