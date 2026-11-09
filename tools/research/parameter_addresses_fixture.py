"""Exercise recovered parameter addresses together with ABI2/4 parameter values."""
import argparse
import json
import shutil
from pathlib import Path
import parameter_values_fixture as parameters
import machine_pipeline_fixture as board


def expected(kind, seed):
    result = parameters.expected(kind, seed)
    if kind:
        return result
    tag, x = 0x81020300 + seed, 0xabcdef80 + seed
    a, b = 0xffe0 + seed, 0x8001 ^ seed
    for n in range(4, 0, -1):
        if (a ^ b) & 1:
            a, b = b, a
        a = (a + n) & 65535
        b ^= tag & 65535
    result ^= tag ^ (a << 16) ^ b
    a = ((seed + (tag & 255)) & 255) ^ 0x81
    b = ((0xff80 + seed + 0x101 + a) & 65535) ^ 0xa55a
    c = ((x ^ tag) + 0x87654321) & 0xffffffff
    p, q = seed + 3, seed ^ 0xa5
    if a & 1:
        p, q = q, p
    result ^= tag ^ 0xa5a5f00d ^ a ^ (b << 16) ^ c ^ p ^ (q << 8)
    after = (((0xff80 + 2 * seed + 0x101) & 65535) ^ 0xa55a) + 0x1234 + seed
    result ^= tag ^ ((0xff80 + seed) << 16) ^ (after & 65535)
    partial = (x ^ (seed << 16)) & 0xffffff00 | ((x + 7) & 255)
    result ^= tag ^ partial
    result ^= tag ^ (((0xffe0 + 2 * seed) & 65535) ^ 0x1357)
    return result & 0xffffffff


def create(output):
    parameters.create(output)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['parameter-addresses.h', 'parameter-addresses-access.c', 'parameter-addresses-cases.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('u32 pv_case(u8);', 'u32 pv_case(u8); u32 pa_case(u8);')
    p.write_text(text.replace('result = pv_case(input);', 'result = pv_case(input) ^ pa_case(input);'))
    config = json.loads((output / 'mx251.json').read_text())
    config['files'] += files
    for name in files[1:]:
        unit = 'u' + str(len(config['units']))
        config['units'].append(dict(name=unit, file=name, backend='ordinary-wide', stackAuto=True, defines=[]))
        config['link'].insert(-1, unit)
    board.write(output / 'mx251.json', config)
    cmake = output / 'CMakeLists.txt'
    cmake.write_text(cmake.read_text().replace('machine-board.c)', 'machine-board.c ' + ' '.join(files) + ')'))
    board.write(output / 'source.json', dict(files={f: board.sha(output / f) for f in config['files']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create':
        create(a.output)
    else:
        board.capture(a.hex, a.output, lambda data: board.check(data, expected))
