"""Ordinary object reuse, byte lanes, alias barriers and allocated addresses."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board

SOURCE = r'''
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u16 FN(value), FN(other), FN(packed);
u8 FN(bytes)[16], FN(small);
volatile u16 FN(observed);

u16 FN(divpair)(u16 x) ABI
{
    u16 q;
    FN(value) = x;
    q = FN(value) / 10u;
    FN(other) = x ^ 0x5a5au;
    return q * 100u + FN(value) % 10u;
}
void FN(mutate)(u16 *p, u16 x) ABI { *p = x; }
u32 FN(call_alias)(u16 x) ABI
{
    u16 before;
    FN(value) = x; before = FN(value);
    FN(mutate)(&FN(value), x + 3u);
    return ((u32)before << 16) | FN(value);
}
u16 FN(store_alias)(u16 *p, u16 x) ABI
{
    FN(value) = x; *p = x ^ 0xa55au; return FN(value);
}
u16 FN(sample)(u16 x) ABI
{
    FN(observed) = x; return FN(observed) + FN(observed);
}
u16 FN(pack)(void) ABI
{
    FN(packed) = ((u16)FN(bytes)[0] << 8) + FN(bytes)[1];
    return FN(packed);
}
u32 FN(addresses)(u8 index) ABI
{
    return ((u32)FN(bytes)[index] << 24) |
        ((u32)FN(bytes)[index + 1] << 16) |
        ((u32)FN(bytes)[index + 2] << 8) | FN(bytes)[index + 3];
}
u32 FN(work)(u8 seed) ABI
{
    u8 i, b = seed * 5u + 0x81u;
    u16 x = (u16)seed * 1021u + 129u;
    u32 result = 0x13579bdfUL;
    for (i = 0; i < 16; ++i) FN(bytes)[i] = seed * 13u + i * 7u;
#define MIX(v) result = (result << 3) ^ (v)
    MIX(FN(divpair)(x));
    MIX(FN(call_alias)(x));
    MIX(FN(store_alias)(&FN(value), x));
    MIX(FN(sample)(x));
    MIX(FN(pack)());
    MIX(FN(addresses)(seed & 7));
    FN(small) = (u8)(x + 5u);
    MIX(FN(small));
    MIX((u8)(b << 3));
    MIX((u8)(b >> 2));
    MIX((u8)((signed char)b >> 3));
    return result;
}
'''


def expected(kind, seed):
    x = (seed * 1021 + 129) & 65535
    d = [(seed * 13 + i * 7) & 255 for i in range(16)]
    b = (seed * 5 + 129) & 255
    values = [(x // 10 * 100 + x % 10) & 65535,
              (x << 16) | ((x + 3) & 65535), x ^ 0xa55a, (x * 2) & 65535,
              d[0] * 256 + d[1], int.from_bytes(bytes(d[seed & 7:(seed & 7)+4]), 'big'),
              (x + 5) & 255, (b << 3) & 255, b >> 2,
              ((b if b < 128 else b - 256) >> 3) & 255]
    result = 0x13579bdf
    for value in values:
        result = ((result << 3) ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    (output/'cases.h').write_text(SOURCE, encoding='ascii', newline='\n')
    (output/'machine-board.h').write_text('typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\n')
    text = (tests/'machine-board.c').read_text()
    text = text.replace('#include "machine-board.h"', '#include "machine-board.h"\nu32 legacy_work(u8); u32 wide_work(u8) __sdcccall(4);')
    begin, end = text.index('        x0 = '), text.index('        TH0 = ')
    text = text[:begin] + text[end:]
    begin, end = text.index('        if (kind == 0)'), text.index('        TR0 = 0; ticks')
    text = text[:begin] + '        result = kind ? wide_work(input) : legacy_work(input);\n' + text[end:]
    (output/'board.c').write_text(text, newline='\n')
    for prefix in ('legacy', 'wide'):
        (output/(prefix+'.c')).write_text('#define FN(name) '+prefix+'_##name\n#define ABI '+
            ('__sdcccall(4)' if prefix == 'wide' else '')+'\n#include "cases.h"\n', newline='\n')
    files = ['cases.h', 'machine-board.h', 'legacy.c', 'wide.c', 'board.c']
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide' if f == 'wide.c' else 'legacy', stackAuto=True, defines=[])
             for i, f in enumerate(files[2:])]
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
        backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard', defines=[], files=files,
        units=units, archives=[], link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json', dict(files={f:board.sha(output/f) for f in files}))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(bundle_values NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create': create(args.output)
    else: board.capture(args.hex, args.output, lambda data: board.check(data, expected))
