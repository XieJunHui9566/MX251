"""Portable byte-bitfield layout tests with independent host and board results."""
import argparse
import json
from pathlib import Path

import machine_pipeline_fixture as board
import user_stress

LAYOUTS = [(start, bits) for start in range(8) for bits in range(1, 9-start)]


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    header = '''#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
#define __data
#define __xdata
#define __edata
#endif
'''
    header += ''.join(f'u32 layout_{k}(u8 seed);\n' for k in range(len(LAYOUTS)))
    header += 'u32 stress_run(u8 kind, u8 seed);\n'
    (root/'stress.h').write_text(header)
    for k, (start, bits) in enumerate(LAYOUTS):
        high = 8-start-bits
        # Alternate legal declared types. No assumptions about host packing.
        base = 'char' if k & 1 else 'int'
        fields = ['u8 prefix;']
        for name, sign in [('value','unsigned'), ('bias','signed')]:
            if start: fields.append(f'unsigned {base} {name}_low : {start};')
            fields.append(f'{sign} {base} {name} : {bits};')
            if high: fields.append(f'unsigned {base} {name}_high : {high};')
            fields.append(f'u8 {name}_guard;')
        qualifier = 'volatile ' if k % 2 else ''
        mode = k % 4
        space = ['__edata', '__xdata', '__data', ''][mode]
        source = '#include "stress.h"\ntypedef struct { '+' '.join(fields)+' } Flags;\n'
        if mode != 3: source += f'static {qualifier}{space} Flags flags[2];\n'
        source += f'u32 layout_{k}(u8 seed) {{\n'
        if mode == 3: source += f'{qualifier}Flags flags[2];\n'
        if mode == 2:
            expr = 'flags[0].'  # Fixed near DATA address, distinct import path.
        else:
            source += f'{qualifier}Flags *p = &flags[seed & 1], *alias = p;\n'
            expr = 'alias->'
        source += 'u8 i, v; u16 input; s16 signed_input; u32 result = 0x12345678UL;\n'
        source += f'{expr}prefix=seed; {expr}value_guard=0x5a; {expr}bias_guard=0xa5;\n'
        for name in ('value','bias'):
            if start: source += f'{expr}{name}_low=seed;\n'
            if high: source += f'{expr}{name}_high=seed^0x55;\n'
        source += f'{expr}value={(1 << bits)-1}; {expr}bias=-1;\n'
        source += f'result ^= {expr}value; result += (u32)(s16){expr}bias;\n'
        source += 'for(i=0;i<4;++i) { v=(u8)((u16)seed*4+i); input=(u16)v+0x100;\n'
        source += f'signed_input=(s16)(v & {(1 << bits)-1})-{1 << (bits-1)};\n'
        source += f'{expr}value=input; {expr}bias=signed_input;\n'
        for field in ['value','bias','prefix','value_guard','bias_guard'] + [
                n+s for n in ('value','bias') for s, width in [('_low',start),('_high',high)] if width]:
            cast = '(u32)(s16)' if field == 'bias' else '(u32)'
            source += f'result=(result<<3)^result^{cast}{expr}{field};\n'
        source += '} return result; }\n'
        (root/f'layout_{k:02}.c').write_text(source)
    dispatch = '#include "stress.h"\nu32 stress_run(u8 kind,u8 seed) { switch(kind) {\n'
    dispatch += ''.join(f'case {k}: return layout_{k}(seed);\n' for k in range(len(LAYOUTS)))
    (root/'dispatch.c').write_text(dispatch+'default: return 0; } }\n')
    board.write(root/'source.json', dict(layouts=LAYOUTS, generatorSha256=board.sha(Path(__file__)),
        files={p.name:board.sha(p) for p in sorted(root.iterdir()) if p.suffix in ('.c','.h')}))


def reference(root, gcc):
    generate(root)
    (root/'host.c').write_text('#include <stdio.h>\n#include "stress.h"\nint main(void) { '
        f'unsigned k,s; for(k=0;k<{len(LAYOUTS)};++k) for(s=0;s<64;++s) '
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s)); return 0; }\n')
    user_stress.run([gcc, '-std=c99', '-O2', '-Wall', *sorted(p.name for p in root.glob('*.c')),
                     '-o', 'reference.exe'], root, label='compile')
    values = {k:int(v) for k,v in (line.split() for line in user_stress.run([root/'reference.exe'], root).splitlines())}
    board.write(root/'reference.json', values)


def create_board(root):
    generate(root)
    source = (user_stress.SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end = source.index('        x0 = '),source.index('        TH0 = ')
    source = source[:start]+source[end:]
    start,end = source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source = source[:start]+'        result = stress_run(kind,input);\n'+source[end:]
    source = source.replace('kind < 2',f'kind < {len(LAYOUTS)}')
    source = source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (root/'board.c').write_text(source)
    files = sorted(p.name for p in root.iterdir() if p.suffix in ('.c','.h'))
    units = [dict(name='u'+str(i),file=n,backend='ordinary-wide',stackAuto=True,defines=[])
             for i,n in enumerate(files) if n.endswith('.c')]
    board.write(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(bitfield_layouts NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


def capture(hexfile, root, reference_path):
    values = json.loads(reference_path.read_text())
    def check(data):
        seen = {}; mismatches = []
        for i in range(len(data)-15):
            frame = data[i:i+16]
            if frame[:4]!=b'MIR1' or frame[-2:]!=b'\r\n': continue
            checksum = 0
            for octet in frame[:14]: checksum ^= octet
            if checksum: continue
            key = f'{frame[4]}:{frame[5]}'
            actual = int.from_bytes(frame[6:10], 'big')
            if actual != values[key] and key not in seen:
                mismatches.append(dict(key=key,actual=actual,expected=values[key]))
            seen[key] = actual
        return dict(status='PASS' if len(seen)==len(values) and not mismatches else 'FAIL',
            cases=len(seen),expectedCases=len(values),mismatches=mismatches,values=seen,timingQualified=False)
    board.capture(hexfile, root, check, max_frames=len(values)*2)
    result = json.loads((root/'result.json').read_text())
    assert result['status']=='PASS', (result['cases'],result['mismatches'][:8])


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('reference');p.add_argument('--output',type=Path,required=True);p.add_argument('--gcc',type=Path,required=True)
    p=sub.add_parser('board');p.add_argument('--output',type=Path,required=True)
    p=sub.add_parser('capture')
    for name in ('hex','output','reference'):p.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    if args.command=='reference':reference(args.output,args.gcc)
    elif args.command=='board':create_board(args.output)
    else:capture(args.hex,args.output,args.reference)
