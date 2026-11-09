"""Check promoted stack parameters under ABI2 and ABI4 on AI8051U."""
import argparse
import shutil
from pathlib import Path
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    mask = 0xffffffff
    u32 = lambda x: x & mask
    legacy = lambda a, b: u32((a ^ b) + 0x87654321)
    a, b = 0x81020300 + seed, 0x7ffe4500 ^ (seed << 9) ^ seed
    word = ((b ^ 0x80008000) ^ a) & 65535
    if word & 0x8000:
        word -= 65536
    values = [u32(((legacy(a, b) & 255) ^ (b & 255)) + b + a),
              u32(word) ^ ((a >> 8) & 255) ^ b,
              u32(legacy(a ^ b, b) + a) ^ (a & 255) ^ (b & 65535)]
    pointer = u32(((a ^ b) & 255 if b & 1 else b & 255) + (a & 255) + 0x3b + 0xa65c + a)
    pa, qa = a & 0xffffff, b & 0xffffff
    address = pa ^ (pa if b & 1 else qa) ^ qa ^ (a ^ b) ^ 0x3b ^ 0xa65c ^ 0xc7
    values += [pointer ^ address, b if (a ^ 0x3b ^ 0xa65c) & 1 else a]
    c, d, e = (a + b) & 255, (a ^ b) & 65535, legacy(a ^ b, b)
    f, g, h = ((a >> 8) & 255) ^ c, ((b >> 16) + d) & 65535, u32(a + b + e)
    values += [u32(c + d + e + f + g + h + a)]
    c, d, n = a ^ b, b & 65535, a & 7
    while n:
        c = u32((c << 1) ^ u32(b + n)); d = (d + c) & 65535; n -= 1
    values += [u32(c + d + a)]

    def recurse(a, b, n, c):
        c = legacy(c, b)
        if n:
            c = recurse(u32(a + 1), b ^ c, n - 1, c)
        return u32(c + a + 0xa65c)

    values += [recurse(a, b, a & 3, a ^ b),
               u32(legacy(a ^ b, b) + sum(((a ^ b) + i) & 255 for i in range(9)) + a + b + 0x3b + 0xa65c)]
    terms = [a + 1, a + 0x12345678, b ^ 0xa5a5a5a5, b + 0x10001,
             a ^ 0xdeadbeef, a + b, a - b, b + 0x87654321]
    values += [u32(legacy(a ^ b, b) + sum(terms) + 0x3b + 0xa65c)]
    octet = lambda i: ((i * 23) ^ 0xa9) & 255
    x, pos, tag = 0xff80 + seed, seed & 7, a
    for _ in range(4):
        x = (x + octet(pos)) & 65535
        if x & 1:
            pos += 1; tag ^= x
        else:
            pos += 2; tag = u32(tag + x)
    values += [tag ^ (x << 16) ^ octet(pos)]
    table = [7,13,29,31,43,61,67,79,83,97,101,127,131,149,157,173,
             179,191,193,211,223,227,229,233,239,241,251,3,5,11,17,19]
    pos, tag = seed & 7, a
    for _ in range(4):
        tag = u32((tag << 1) ^ table[pos]); pos += 1
    values += [tag ^ table[pos]]
    x, tag = -100 + seed, a
    for _ in range(5):
        tag ^= u32(x); x += 3
    values += [tag ^ u32(x)]
    before, after = 0xff80 + seed, (0xff80 + 2 * seed + 0x101) & 65535
    values += [a ^ (after ^ 0xa55a) ^ (before << 16) ^ after]
    x, tag = 0xff80 + seed, a
    for _ in range(5):
        which = x & 7
        if which == 0: x += 17
        elif which == 1: x ^= 0x138
        elif which == 2: x -= 9
        elif which == 3: x += 0x8001
        elif which == 4: x ^= 0x7ffe
        elif which == 5: x += 3
        elif which == 6: x -= 0x101
        else: x ^= 0xa5a5
        x &= 65535
        tag = u32((tag << 1) ^ x)
    values += [tag ^ x]
    result = 0x13579bdf
    for value in values:
        result = u32((result << 3) ^ result ^ value)
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'wide-parameter-write.h', 'parameter-values.h',
             'wide-parameter-write-callees.c', 'wide-parameter-write-callers.c',
             'parameter-values-access.c', 'parameter-values-cases.c', 'machine-control.c', 'machine-board.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('#include "machine-board.h"',
        '#include "machine-board.h"\nu16 POINT_COLOR; u32 trace;\nu32 pv_case(u8); void pv_init(void);')
    text = text.replace('    for (;;) for (kind', '    pv_init();\n    for (;;) for (kind')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = pv_case(input);')
    p.write_text(text)
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f == 'machine-board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[3:])]
    board.write(output / 'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=True, standard='sdcc11', runtime='standard',
        defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup']))
    (output / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(parameters NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output / 'source.json', dict(files={f: board.sha(output / f) for f in files}))


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
