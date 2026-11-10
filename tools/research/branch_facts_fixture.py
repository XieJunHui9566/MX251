"""Run the same branch/state workload through legacy and wide code generation."""
import argparse
from pathlib import Path
import re
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    result = 0x13579bdf
    additions = {17: 31, 18: 61, 19: 61, 20: 97, 21: 131, 22: 163,
                 23: 197, 24: 229, 25: 263, 26: 293, 27: 331, 28: 367,
                 29: 401, 30: 431, 31: 467, 32: 499, 34: 541, 35: 577, 36: 601}
    def cast(value):
        saved = value >> 4
        if value & 255 == 0x81:
            return saved
        if value & 255 == 255:
            return saved ^ value
        return value
    for i in range(4):
        x = seed * 4 + i
        word = (x << 8) | (x ^ 0x91)
        mask = word & 0xfffd if x in (1, 129) else word ^ x
        result = (((result << 3) ^ result) & 0xffffffff) ^ mask
        result += word & 0xfffd if x == 129 else (word + x) & 65535
        table = ((1 << (x & 15)) + additions[x]) & 65535 if x in additions else (1 << (x & 15)) ^ 0x7731
        result ^= table
        result += (1 << (x & 15)) + 129 if seed & 1 else (1 << (x & 15)) ^ 1
        value, current = 0, x
        for n in range(5):
            value = (value + (1 << (x & 15))) & 65535 if current == 129 else value ^ current
            current = (current + 33) & 255
        for n in range(3):
            saved = current & 15
            current = (current + 17) & 255
            if current == 129:
                value ^= 1 << saved
        result ^= value
        result += cast(word)
        result ^= cast((x << 8) | 0x81)
        result += 2 if x == 129 else ((x if x < 128 else x - 256) & 65535)
        memory = ((2 << 3) ^ 2 ^ 0x5291) if x == 129 else ((x + 7) & 255) + ((x + 13) & 255)
        if (x + 11) & 255 == 129:
            memory ^= 2
        result ^= memory
        value, state = 0x5173, x & 3
        for n in range(16):
            if state in (0, 2):
                value ^= 1 << state
            elif state == 1:
                value += 2
            else:
                value -= 8
            value &= 65535
            state = (state + x + n) & 3
        result += value
        wide = (x << 24) | 0x234567
        saved = (wide >> 8) ^ 0x003579bd
        result ^= saved if x == 129 else (saved + wide) & 0xffffffff
        result &= 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    source = (tests / 'branch-facts.c').read_bytes()
    (output / 'branch-facts.c').write_bytes(source)
    # Separate translation units force each backend to compile the identical
    # workload, including callees and mutable objects, with separate symbols.
    names = sorted(set(re.findall(rb'\bbf_\w+', source)))
    prefix = b''.join(b'#define ' + n + b' legacy_' + n + b'\n' for n in names)
    (output / 'branch-facts-legacy.c').write_bytes(prefix + source)
    shutil.copy2(tests / 'machine-board.h', output / 'machine-board.h')
    text = (tests / 'machine-board.c').read_text()
    text = text.replace('static u8 checksum;', 'u32 bf_case(u8);\nu32 legacy_bf_case(u8);\nstatic u8 checksum;')
    text = text.replace('POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;', '')
    text = text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;', 'result = bf_case(input);')
    text = text.replace('control(input)', 'legacy_bf_case(input)')
    (output / 'machine-board.c').write_bytes(text.encode())
    files = ['machine-board.h', 'branch-facts.c', 'branch-facts-legacy.c', 'machine-board.c']
    units = [dict(name='u' + str(i), file=f, backend='ordinary-wide' if f == 'branch-facts.c' else 'legacy',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[1:])]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
                  backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
                  defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    board.write(output / 'mx251.json', config)
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
