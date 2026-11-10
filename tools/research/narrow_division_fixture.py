"""Byte/word division on AI8051U, checked against independent Python integers."""
import argparse
from pathlib import Path
import shutil

import machine_pipeline_fixture as board

WORD_DIVISORS = [1, 2, 3, 7, 10, 15, 16, 31, 127, 128, 129, 255, 256, 257, 10000, 65535]
BYTE_DIVISORS = [1, 2, 3, 5, 7, 10, 15, 16, 31, 63, 127, 128, 129, 200, 254, 255]
ENDPOINTS = [0, 1, 255, 256, 32767, 32768, 65534, 65535]


def fold(result, value):
    return ((result << 5) ^ result ^ value) & 0xffffffff


def expected(kind, seed):
    result = 0x13579bdf
    if kind:
        for a in range(seed * 4, seed * 4 + 4):
            for b in BYTE_DIVISORS:
                result = fold(fold(result, a // b), a % b)
        return result
    a = ENDPOINTS[seed & 7] if seed < 32 else (65535 - seed * 1031) & 65535
    b = WORD_DIVISORS[seed & 15]
    narrow = (b & 255) | 1
    live = [a, a ^ 0xa55a, (a + 1) & 65535, (a - 1) & 65535,
            a ^ 65535, (a * 3) & 65535, a ^ 0x8000]
    q = [v // b for v in live]
    live_result = q[6]
    for i in range(6):
        live_result ^= q[i] << (i + 1)
    for v in live[:4]:
        live_result ^= v % b
    decimal = sum(int(c) << (4 * i) for i, c in enumerate(reversed(str(a))))
    values = [a // b, a % b, (a // b) & 255, (a % b) & 255,
              (a // narrow + a % narrow) & 65535,
              ((a & 255) // b + (a & 255) % b) & 65535,
              decimal, live_result & 65535, (a // b) ^ ((a ^ 0xa55a) % b)]
    for value in values:
        result = fold(result, value)
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    for name in ['native-narrow-division.c', 'native-narrow-division.h', 'machine-board.h']:
        shutil.copy2(tests / name, output / name)
    array = lambda values: ','.join(str(v) + 'u' for v in values)
    driver = '''#include "native-narrow-division.h"
static div_u32 mix(div_u32 hash, div_u32 value) { return (hash << 5) ^ hash ^ value; }
static const div_u16 divisors[16] = {WORD_DIVISORS};
static const div_u16 endpoints[8] = {ENDPOINTS};
static const div_u8 byte_divisors[16] = {BYTE_DIVISORS};
div_u32 division_words(div_u8 seed)
{
    div_u16 a = seed < 32 ? endpoints[seed & 7] : (div_u16)(65535u - (div_u16)seed * 1031u);
    div_u16 b = divisors[seed & 15];
    div_u8 narrow = (div_u8)b | 1u;
    div_u32 hash = 0x13579bdfUL;
    hash = mix(hash, div_q16(a,b));
    hash = mix(hash, div_r16(a,b));
    hash = mix(hash, div_narrow_q(a,b));
    hash = mix(hash, div_narrow_r(a,b));
    hash = mix(hash, div_mixed(a,narrow));
    hash = mix(hash, div_byte_word((div_u8)a,b));
    hash = mix(hash, div_decimal(a));
    hash = mix(hash, div_live(a,b,a^0xa55au,a+1u,a-1u,a^65535u,a*3u,a^0x8000u));
    return mix(hash, div_volatile(a,b));
}
div_u32 division_bytes(div_u8 seed)
{
    div_u8 j, k, a, b;
    div_u32 hash = 0x13579bdfUL;
    for (j = 0; j < 4; ++j) for (k = 0; k < 16; ++k) {
        a = seed * 4u + j; b = byte_divisors[k];
        hash = mix(hash, div_q8(a,b));
        hash = mix(hash, div_r8(a,b));
    }
    return hash;
}
'''.replace('WORD_DIVISORS', array(WORD_DIVISORS)).replace('BYTE_DIVISORS', array(BYTE_DIVISORS)).replace('ENDPOINTS', array(ENDPOINTS))
    (output / 'division-driver.c').write_text(driver, encoding='utf8', newline='\n')
    text = (tests / 'machine-board.c').read_text()
    text = text.replace('static u8 checksum;', 'u32 division_words(u8);\nu32 division_bytes(u8);\nstatic u8 checksum;')
    start, end = text.index('        x0 ='), text.index('        TH0 =')
    text = text[:start] + text[end:]
    start, end = text.index('        if (kind == 0)'), text.index('        TR0 = 0; ticks')
    text = text[:start] + '        result = kind ? division_bytes(input) : division_words(input);\n' + text[end:]
    text = text.replace('ticks, x0, y0, x1, y1, x2, y2', 'ticks')
    (output / 'division-board.c').write_text(text, encoding='utf8', newline='\n')
    sources = ['native-narrow-division.c', 'division-driver.c', 'division-board.c']
    files = sources + ['native-narrow-division.h', 'machine-board.h']
    units = [dict(name='u'+str(i), file=name, backend='ordinary-wide' if i == 0 else 'legacy',
                  stackAuto=True, defines=[]) for i, name in enumerate(sources)]
    board.write(output / 'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
                optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
                runtime='standard', defines=[], files=files, units=units, archives=[],
                link=[u['name'] for u in units]+['@startup']))
    (output / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(narrow_division NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE ' + ' '.join(files) + ')\nmx251_firmware(app)\n', encoding='utf8', newline='\n')
    board.write(output / 'source.json', dict(files={name: board.sha(output / name) for name in files}))


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
