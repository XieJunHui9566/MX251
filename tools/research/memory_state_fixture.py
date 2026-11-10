"""Exercise storage and live values across EAXFR on AI8051U-34K64-32."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


def expected(mode, seed):
    a, b, c, d = 0x12345678 + seed, 0x87654321 ^ seed, 0x13579bdf, 0x2468ace0 ^ mode
    for i in range(8):
        lo_a, lo_b = a & 65535, b & 65535
        b = (b + ((seed+i+lo_b) & 65535)) & 0xffffffff
        c ^= ((seed ^ (i*7)) << 16) | lo_a
        value = (a + seed + i) & 0xffffffff
        a = ((value << 5) ^ (value >> 7) ^ 0x31415926) & 0xffffffff
        d = ((d << 3) ^ (d >> 5) ^ b) & 0xffffffff
        a ^= (c+d) & 0xffffffff
    return a ^ b ^ c ^ d


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    (output/'memory-state.c').write_bytes((tests/'memory-state.c').read_bytes())
    (output/'state-helper.c').write_text(
        'unsigned long state_mix(unsigned long x) { return (x << 5) ^ (x >> 7) ^ 0x31415926UL; }\n')
    (output/'machine-board.h').write_text(
        'typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\n'
        'u32 state_work(u8,u8);\n')
    source = (tests/'machine-board.c').read_text()
    begin, end = source.index('        x0 = '), source.index('        TH0 = ')
    source = source[:begin]+source[end:]
    begin, end = source.index('        if (kind == 0)'), source.index('        TR0 = 0; ticks')
    source = source[:begin]+'        result = state_work(input, kind);\n'+source[end:]
    (output/'state-board.c').write_text(source, newline='\n')
    files = ['memory-state.c', 'state-helper.c', 'state-board.c', 'machine-board.h']
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f=='state-board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i,f in enumerate(files[:3])]
    board.write(output/'mx251.json', dict(schema=2,target='AI8051U-34K64-32',model='small',
        optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',
        defines=[],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(memory_state NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(files={name:board.sha(output/name) for name in files}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
