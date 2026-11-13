"""Exercise full-object argument loads across ABI2/ABI4 and mutating callees."""
import argparse
import json
from pathlib import Path
import machine_pipeline_fixture as board
import native_call_composition_fixture as native


def expected(kind, seed):
    if not kind:
        return native.expected(kind, seed)
    mask = 0xffffffff
    value = 0xfedcba98 ^ (0x01010101 * seed)
    word = (0xffe0 + seed) & 65535
    signed = (-1234567 - seed * 17) & mask
    bits = 0xbfc00000 if seed & 1 else 0x40500000
    result = 0x13579bdf
    for _ in range(4):
        part = ((value + (word << 16)) & mask) ^ signed ^ bits
        word ^= value & 65535
        value = (value + 0x01020304) & mask
        result = ((result << 3) ^ part) & mask
        part = ((value ^ result) + (word << 16) + 3) & mask
        value = (value + 0x01020304) & mask
        word ^= result & 65535
        result ^= part
    return result ^ value ^ word


def create(output):
    native.create(output)
    (output/'argument-objects.h').write_text('''#include "native-call-composition.h"
extern __data signed long signed_argument;
extern __data float float_argument;
u32 ordinary_mix(u32 a, u16 b, signed long c, float d);
u32 argument_work(u8 seed);
''')
    (output/'argument-objects.c').write_text('''#include "argument-objects.h"
__data signed long signed_argument;
__data float float_argument;

u32 argument_work(u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL;
    sample_a = 0xfedcba98UL ^ (0x01010101UL * seed);
    sample_word = (u16)(0xffe0u + seed);
    signed_argument = -1234567L - (signed long)seed * 17;
    float_argument = (seed & 1) ? -1.5f : 3.25f;
    for (i = 0; i < 4; ++i) {
        result = (result << 3) ^ ordinary_mix(sample_a, sample_word, signed_argument, float_argument);
        result ^= native_mix(sample_a, result, sample_word);
    }
    return result ^ sample_a ^ sample_word;
}
''')
    (output/'argument-sink.c').write_text('''#include "argument-objects.h"

u32 ordinary_mix(u32 a, u16 b, signed long c, float d)
{
    union { float number; u32 bits; } value;
    value.number = d;
    sample_a += 0x01020304UL;
    sample_word ^= (u16)a;
    return (a + ((u32)b << 16)) ^ (u32)c ^ value.bits;
}
''')
    path = output/'machine-board.c'
    text = path.read_text().replace('u32 native_work(u8);', 'u32 native_work(u8); u32 argument_work(u8);')
    path.write_text(text.replace('else result = control(input);', 'else result = argument_work(input);'))
    config = json.loads((output/'mx251.json').read_text())
    config['files'] += ['argument-objects.h', 'argument-objects.c', 'argument-sink.c']
    config['files'].remove('machine-control.c')
    config['units'] = [u for u in config['units'] if u['file'] != 'machine-control.c']
    for index, source in enumerate(['argument-objects.c', 'argument-sink.c']):
        config['units'].append(dict(name='argument'+str(index), file=source,
                                    backend='ordinary-wide', stackAuto=True, defines=[]))
    config['link'] = [u['name'] for u in config['units']] + ['@startup']
    board.write(output/'mx251.json', config)
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(argument_objects NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(config['files'])+')\nmx251_firmware(app)\n')
    board.write(output/'source.json', dict(files={f: board.sha(output/f) for f in config['files']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    command = sub.add_parser('create'); command.add_argument('--output', type=Path, required=True)
    command = sub.add_parser('capture')
    command.add_argument('--hex', type=Path, required=True)
    command.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.output)
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, expected))
