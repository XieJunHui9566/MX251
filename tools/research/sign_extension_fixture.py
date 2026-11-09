"""All int16 bit patterns and mixed-width calls on the existing board harness."""
import argparse
from pathlib import Path
import runtime_effects_fixture as arithmetic
import machine_pipeline_fixture as board


def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def expected(kind, seed):
    if not kind:
        result = 0x13579bdf
        for i in range(1024):
            value = signed((seed << 10) | i, 16)
            result = (((result ^ value) * 16777619) ^ signed(value, 8)) & 0xffffffff
        return result
    a, b, c, result = signed(0x8000 + seed * 997, 16), signed(0x7fff - seed * 419, 16), signed(seed * 37, 8), seed
    for _ in range(128):
        result = a + b + c + (result & 0x7fff)
        a, b = signed(a + 1237, 16), signed(b - 173, 16)
    return result & 0xffffffff


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    create = sub.add_parser('create'); create.add_argument('--output', type=Path, required=True)
    capture = sub.add_parser('capture')
    capture.add_argument('--hex', type=Path, required=True); capture.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.command == 'create':
        arithmetic.create(args.output)
        source = board.REPO / 'core/sources/compiler/src/mcs251/tests/sign-extension.c'
        (args.output / 'runtime-arithmetic.c').write_bytes(source.read_bytes())
        board.write(args.output / 'source.json', dict(files={name: board.sha(args.output / name)
            for name in ('runtime-arithmetic.c', 'runtime-board.c', 'machine-board.h')}))
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, expected))
