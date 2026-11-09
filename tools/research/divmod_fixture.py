"""Portable div/rem boundary workloads using the established UART board harness."""
import argparse
from pathlib import Path
import runtime_effects_fixture as arithmetic
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        a, b = -(1 << 31) + seed * 1234567, seed * 1031 + 2
        def pair(a, b):
            q = abs(a) // abs(b) * (-1 if (a < 0) != (b < 0) else 1)
            return ((q << 3) ^ (a - q * b)) & 0xffffffff
        return pair(a, b) ^ pair(a, -b) ^ pair(-(a + 1), b) ^ pair(-(a + 1), -b)
    a, b = 0xffffffff - seed * 0x1020304, seed * 4 + 1
    q, r = divmod(a, b)
    results = [(q << 3) ^ r, q + r + r, q + a % (b + 1),
               q + ((a + 1) & 0xffffffff) % b, (q if a & 1 else 1) + r,
               q + a % (b + 1), q if q & 1 else q + r, q, r]
    value = 0
    for result in results: value ^= result & 0xffffffff
    return value


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    create = sub.add_parser('create'); create.add_argument('--output', type=Path, required=True)
    capture = sub.add_parser('capture')
    capture.add_argument('--hex', type=Path, required=True)
    capture.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.command == 'create':
        arithmetic.create(args.output)
        source = board.REPO / 'core/sources/compiler/src/mcs251/tests/wide-divmod.c'
        (args.output / 'runtime-arithmetic.c').write_bytes(b'#define ABI\n' + source.read_bytes())
        board.write(args.output / 'source.json', dict(files={name: board.sha(args.output / name)
            for name in ('runtime-arithmetic.c', 'runtime-board.c', 'machine-board.h')}))
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, expected))
