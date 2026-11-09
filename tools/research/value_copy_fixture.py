"""Independent result and timer checks for overlapping MIR value copies."""
import argparse
import shutil
from pathlib import Path
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    mask = 0xffffffff
    octet = lambda i: ((i * 29) ^ 0xa7) & 255
    a, b, mode, bias = (0xffe0 + seed) & 65535, 0x9876 ^ seed, seed ^ 0x5a, 0x81020304 + seed
    result = 0x2468ace0
    for i in range(16):
        which = (seed + i) & 3
        part = (bias ^ (((a + (3, 5, 7, 9)[which]) & 65535) << 16)
                ^ (b ^ (octet((seed & 15) + i) + (11, 13, 17, 19)[which]))
                ^ (mode << (4, 8, 12, 16)[which]))
        result = ((result << 3) ^ result ^ part) & mask
    total, x = 0, a
    for i in range(16):
        saved, x = x, (x + 3) & 65535
        if (seed + i) & 1:
            x ^= 0x8123
        total ^= (saved + x) & 65535
        if (seed + i) & 2:
            total = (total + saved) & 65535
    values = (total, (0xff80 + seed) ^ ((0xff80 + 2 * seed + 0x101) & 65535),
              (octet(seed & 15) << 8) | octet((seed & 15) + (seed & 7) + 1))
    for value in values:
        result = ((result << 5) ^ result ^ value) & mask
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'value-copy.h', 'value-copy-access.c', 'value-copy-sinks.c',
             'value-copy-cases.c', 'machine-control.c', 'machine-board.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('#include "machine-board.h"',
                                '#include "value-copy.h"\nu16 POINT_COLOR; u32 trace;')
    text = text.replace('    for (;;) for (kind', '    copy_init();\n    for (;;) for (kind')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;',
                        'result = copy_case(input);')
    p.write_text(text)
    units = [dict(name='u' + str(i), file=f, backend='legacy' if f == 'machine-board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[2:])]
    board.write(output / 'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
                optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
                runtime='standard', defines=[], files=files, units=units, archives=[],
                link=[u['name'] for u in units] + ['@startup']))
    (output / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(value_copy NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE ' +
        ' '.join(files) + ')\nmx251_firmware(app)\n')
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
