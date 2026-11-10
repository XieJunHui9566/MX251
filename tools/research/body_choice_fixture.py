"""Exercise complete backend selection with byte control flow and calls."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind:
        return board.expected(kind, seed)
    result, trace = 0x13579bdf, 0x1972513a
    input_flags, output_flags = seed ^ 0xa5, seed ^ 0x5a
    for i in range(8):
        index = (1, 0x81, 0x82, 0x83)[(seed + i) & 3]
        events = [(0, 255)]
        if (seed + i) & 8 and (seed + i) & 16 and (seed + i) & 32:
            if index in (0x81, 0x82):
                input_flags |= 1 << (index & 15)
                events = [(14, index & 15), (17, 16), (14, 0), (0, 0)]
            elif index == 1:
                output_flags |= 2
                events = [(14, 1), (20, 32), (14, 0), (0, 0)]
        for address, value in events:
            trace = ((trace << 5) ^ trace ^ (address << 8) ^ value) & 0xffffffff
        result = (((result << 3) ^ result ^ trace) + (output_flags << 8) + input_flags) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['machine-board.h', 'body-choice-cases.c', 'body-choice-callee.c', 'machine-control.c', 'machine-board.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    p = output / 'machine-board.c'
    text = p.read_text().replace('static u8 checksum;', 'u32 body_case(u8);\nstatic u8 checksum;')
    text = text.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = body_case(input);')
    p.write_bytes(text.encode())
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f in ('machine-board.c', 'body-choice-callee.c') else 'ordinary-wide', stackAuto=True, defines=[]) for i, f in enumerate(files[1:])]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard', defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output/'mx251.json', config)
    board.write(output/'source.json', dict(files={f:board.sha(output/f) for f in files}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, expected))
