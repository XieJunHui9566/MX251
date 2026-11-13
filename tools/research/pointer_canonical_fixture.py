"""Combine existing parameter ABI and bounded cursor hardware oracles."""
import argparse
from pathlib import Path
import shutil
import json
import parameter_values_fixture as parameters
import loop_cursors_fixture as cursors
import machine_pipeline_fixture as board


def expected(kind, seed):
    return cursors.expected(seed & 1, seed) if kind else parameters.expected(0, seed)


def create(output):
    parameters.create(output)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    for name in ('loop-cursors.h', 'loop-cursors.c'):
        shutil.copy2(tests / name, output / name)
    source = output / 'machine-board.c'
    source.write_text(source.read_text().replace('void pv_init(void);',
        'void pv_init(void); u32 cursor_case(u8,u8);').replace('result = control(input);',
        'result = cursor_case(input & 1, input);'), newline='\n')
    config = json.loads((output / 'mx251.json').read_text())
    config['files'] = [f for f in config['files'] if f != 'machine-control.c'] + ['loop-cursors.h', 'loop-cursors.c']
    for unit in config['units']:
        if unit['file'] == 'machine-control.c':
            unit['file'] = 'loop-cursors.c'
    board.write(output / 'mx251.json', config)
    cmake = output / 'CMakeLists.txt'
    cmake.write_text(cmake.read_text().replace('machine-control.c', 'loop-cursors.c loop-cursors.h'), newline='\n')
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
