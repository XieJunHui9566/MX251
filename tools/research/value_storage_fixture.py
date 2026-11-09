"""Exercise value definitions, stack constants and reconstructed call addresses."""
import argparse
import json
import shutil
from pathlib import Path
import parameter_addresses_fixture as parameters
import machine_pipeline_fixture as board


def storage_expected(seed):
    mask = 0xffffffff

    def visit(data, start, length, salt):
        result = 0x13579bdf
        for i in range(length):
            result = ((result << 3) ^ result ^ data[start+i]) & mask
            data[start+i] = (data[start+i] + salt + i) & 255
        return result

    zero = [0]*31
    pattern = [0x12, 0x34, 0x80, 0xff, 0, 0xa5, 0x5a, 0xfe, 0x71]
    buffers = visit(zero, 0, 31, seed)
    pattern[1] ^= seed
    zero[3] = (zero[3]+0x5b) & 255
    pattern[5] ^= 0xa7
    buffers ^= (pattern[1] << 16) ^ (zero[3] << 8) ^ pattern[5]
    pattern[2] = (pattern[2]+seed) & 255
    zero[19] ^= 0x37
    buffers ^= (pattern[2] << 24) ^ (zero[19] << 8) ^ 0x9137
    buffers ^= visit(pattern, 0, 9, seed+17) ^ visit(zero, 0, 31, seed+5)
    a, b, result = 0xff80+seed, 0x8001 ^ seed, 0x2468ace0
    for i in range(6):
        if (a ^ b) & 1:
            a, b = b, a
        branch = (seed+i) & 3
        if branch == 0:
            c, b = a, (b+a) & 65535
        elif branch == 1:
            c, a = b, a ^ b
        elif branch == 2:
            c, a = (a+b) & 65535, (a-7) & 65535
        else:
            c, b = (a-b) & 65535, b ^ 0x8123
        result = ((result << 3) ^ result ^ (c << 16) ^ a ^ b) & mask
    result ^= (b << 16) ^ a

    def recurse(salt, depth):
        data = [3, 0xf1, 0x82, 7, 0, 0xff, 0x56]
        r = visit(data, 0, 7, salt)
        if depth:
            r ^= recurse(salt+3, depth-1)
        return r ^ visit(data, 1, 5, salt+7)

    volatile = [1, 3, 5, 7, 9]
    volatile[seed % 5] ^= seed
    observed = (volatile[0] << 24) ^ (volatile[1] << 16) ^ (volatile[2] << 8) ^ volatile[3] ^ volatile[4]
    return buffers ^ result ^ recurse(seed, seed & 3) ^ observed


def expected(kind, seed):
    return parameters.expected(kind, seed) ^ (storage_expected(seed) if not kind else 0)


def create(output):
    parameters.create(output)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['value-storage.h', 'value-storage.c', 'value-storage-sinks.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('u32 pa_case(u8);', 'u32 pa_case(u8); u32 vs_case(u8);')
    p.write_text(text.replace('pv_case(input) ^ pa_case(input)', 'pv_case(input) ^ pa_case(input) ^ vs_case(input)'))
    config = json.loads((output / 'mx251.json').read_text())
    config['files'] += files
    for name in files[1:]:
        unit = 'u' + str(len(config['units']))
        config['units'].append(dict(name=unit, file=name, backend='ordinary-wide', stackAuto=True, defines=[]))
        config['link'].insert(-1, unit)
    board.write(output / 'mx251.json', config)
    cmake = output / 'CMakeLists.txt'
    cmake.write_text(cmake.read_text().replace('machine-board.c ', 'machine-board.c '+' '.join(files)+' '))
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
