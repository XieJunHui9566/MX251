"""Exercise dereference address widths across byte boundaries on AI8051U."""
import argparse
import shutil
from pathlib import Path
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    octet = lambda i: ((i * 17) ^ (i >> 8) ^ 0x5a) & 255
    values = [octet(2 * (64 + seed)), octet(2 * (128 + seed)),
              octet(2 * (1536 + seed)), octet(2048 - 2 * (129 + seed)),
              octet(2 * (256 + seed)), octet(3 * (257 + seed)),
              octet(2 * (256 + seed)), octet(2 * (256 + seed)),
              (0x3456 ^ (2 * (256 + seed) * 29)) & 255,
              octet((2 * (128 + seed)) & 255), seed ^ 0xa5]
    result = 0x13579bdf
    for value in values:
        result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'pointer-stride.h', 'pointer-stride-access.c',
             'pointer-stride-cases.c', 'machine-control.c', 'machine-board.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('#include "machine-board.h"',
                                '#include "pointer-stride.h"\nu16 POINT_COLOR; u32 trace;')
    text = text.replace('    for (;;) for (kind', '    stride_init();\n    for (;;) for (kind')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;',
                        'result = stride_case(input);')
    p.write_text(text)
    units = [dict(name='u' + str(i), file=f, backend='legacy' if f == 'machine-board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[2:])]
    board.write(output / 'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
                optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
                runtime='standard', defines=[], files=files, units=units, archives=[],
                link=[u['name'] for u in units] + ['@startup']))
    (output / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(stride NONE)\n'
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
