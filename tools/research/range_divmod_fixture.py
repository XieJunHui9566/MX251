"""Check range-proven division and signed boundaries against an independent host oracle."""
import argparse
from pathlib import Path

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def create(root, gcc):
    root.mkdir(parents=True, exist_ok=False)
    host, project = root / 'host', root / 'board'
    host.mkdir(); project.mkdir()
    source = user_stress.SOURCES.parent / 'range-native-divmod.c'
    for folder in (host, project):
        (folder / 'work.c').write_bytes(source.read_bytes())
    header = '''#ifdef __SDCC
typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;
#else
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#endif
u32 stress_run(u8 kind,u8 seed);
'''
    for folder in (host, project): (folder / 'stress.h').write_text(header)
    (host / 'main.c').write_text('#include <stdio.h>\n#include "stress.h"\n'
        'int main(void) {unsigned k,s;for(k=0;k<8;k++)for(s=0;s<64;s++)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc, '-std=c99', '-O2', '-Wall', 'main.c', 'work.c', '-o', 'reference.exe'], host)
    values = {k:int(v) for k,v in (line.split() for line in
              user_stress.run([host / 'reference.exe'], host).splitlines())}
    board.write(root / 'reference.json', values)
    main = (user_stress.SOURCES.parent / 'machine-board.c').read_text().replace('"machine-board.h"', '"stress.h"')
    start, end = main.index('        x0 = '), main.index('        checksum = 0;')
    main = main[:start]+'        result=stress_run(kind,input); ticks=0; overflow=0;\n'+main[end:]
    main = main.replace('kind < 2', 'kind < 8').replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (project / 'board.c').write_text(main)
    files = ['board.c', 'work.c', 'stress.h']
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i,f in enumerate(files[:2])]
    board.write(project / 'mx251.json', dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=['u0','u1','@startup']))
    (project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(range_divmod NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE board.c work.c stress.h)\nmx251_firmware(app)\n')
    board.write(root / 'source.json', dict(source=board.sha(source), cases=512,
        byteInputs=256, innerInputsPerCase=4, wordBoundaries=[32767,32768,65534,65535],
        hardwareExecuted=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--gcc', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True)
    for c in sub.choices.values(): c.add_argument('--root', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.root, a.gcc)
    else: bitfield_fixture.capture(a.hex, a.root / 'capture', a.root / 'reference.json')
