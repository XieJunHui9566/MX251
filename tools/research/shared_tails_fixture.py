"""Parameterized volatile tail sharing with independent board expectations."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    registers = [(seed * 13 + i * 7) & 255 for i in range(32)]
    trim, vtrim, band, frequency = seed ^ 0x55, seed ^ 0xaa, seed * 3, 1234567 + seed
    if kind:
        mode = seed & 7
        if mode < 4:
            trim, vtrim = registers[16 + 2 * mode:18 + 2 * mode]
            band = (band & 0xfc) | [1, 2, 3, 2][mode]
            registers[24] = 1
            frequency = [24000000, 32000000, 40000000, 22118400][mode]
    else:
        channel, pwm = seed & 3, (seed * 37) ^ 0x5a3
        if channel < 3:
            high, low = channel * 4 + 1, channel * 4
            registers[high] = (registers[high] & 0xcd) | ((pwm & 0x300) >> 4) | ((pwm & 0x400) >> 9)
            registers[low] = pwm & 255
    result = 0x13579bdf ^ seed
    word = (seed * 997) & 65535
    value = ((word + (3 if seed & 1 else 7)) & 65535) ^ (0x1234 if seed & 1 else 0x5678)
    result ^= value
    registers[12 if seed & 1 else 13] = value & 255
    word = (seed * 1073) & 65535
    value = (word + (8 if seed & 2 else 5)) & 65535
    result ^= value << 16
    registers[14 if seed & 2 else 15] = (word + 3) & 255
    for value in registers + [trim, vtrim, band]:
        result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result ^ frequency


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    files = ['shared-tails.h', 'shared-tails-callees.c', 'shared-tails-values.c', 'shared-tails-callers.c']
    for name in files:
        shutil.copy2(tests / name, output / name)
    source = (tests / 'machine-board.c').read_text().replace('machine-board.h', 'shared-tails.h')
    start, end = source.index('        x0 = '), source.index('        TH0 = ')
    source = source[:start] + source[end:]
    start, end = source.index('        if (kind == 0)'), source.index('        TR0 = 0; ticks')
    source = source[:start] + '        result = tail_case(kind, input);\n' + source[end:]
    source = source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output / 'tail-board.c').write_text(source, newline='\n')
    files.append('tail-board.c')
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i, f in enumerate(files) if f.endswith('.c')]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
        backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
        defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output / 'mx251.json', config)
    board.write(output / 'source.json', dict(files={f:board.sha(output / f) for f in files}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('capture'); p.add_argument('--hex', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.output)
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, expected))
