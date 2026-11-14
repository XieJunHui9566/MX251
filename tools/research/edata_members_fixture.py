"""Run EDATA member and initializer-only formatting cases against host C results."""
import argparse
import json
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    files = ['edata-members.h', 'edata-members.c', 'edata-members-mutate.c',
             'edata-members-init.c', 'edata-members-format.c']
    for name in files:
        shutil.copy2(tests/name, output/name)
    text = (tests/'machine-board.c').read_text().replace('"machine-board.h"', '"edata-members.h"')
    start = text.index('        x0 = 10 + input;')
    end = text.index('        TH0 = 0;', start)
    text = text[:start] + text[end:]
    start = text.index('        if (kind == 0) {')
    end = text.index('        TR0 = 0; ticks', start)
    text = text[:start] + '        result = kind ? member_native(input) : member_work(input);\n' + text[end:]
    text = text.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'member-board.c').write_text(text)
    files.append('member-board.c')
    units = [dict(name='u'+str(i), file=name, stackAuto=True, defines=[], backend='ordinary-wide')
             for i,name in enumerate(files) if name.endswith('.c')]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
        backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
        defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output/'mx251.json', config)
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(edata_members NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output/'source.json', dict(files={f:board.sha(output/f) for f in files}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture')
    for name in ('hex', 'reference', 'output'):
        c.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.output)
    else:
        reference = json.loads(args.reference.read_text())
        board.capture(args.hex, args.output, lambda data: board.check(data, lambda k,s: reference[f'{k}:{s}']))
