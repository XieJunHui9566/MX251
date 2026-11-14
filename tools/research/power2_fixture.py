"""Execute promoted division/modulo and signed boundaries on the board."""
import argparse
from pathlib import Path

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def create(root, gcc):
    root.mkdir(parents=True, exist_ok=False)
    source = user_stress.SOURCES.parent / 'runtime-power2.c'
    host, project = root / 'host', root / 'board'
    host.mkdir(); project.mkdir()
    work = source.read_text() + '''
u32 stress_run(u8 kind, u8 seed) {
    u8 i, x; u16 w; s16 s; u32 r=0x12345678UL, a;
    for(i=0;i<4;++i) {
        x=(u8)(seed*4U+i); w=(u16)x*257U;
        if(x==254)w=65534U;
        if(x==255)w=65535U;
        s=x==0 ? -32767-1 : x==255 ? 32767 : (s16)((s32)x*251L-32000L);
        if(kind==0)a=((u32)promote_mod(x)<<16)^promote_div(x);
        else if(kind==1)a=(wide_mod(w)<<16)^wide_div(w);
        else if(kind==2)a=((u32)(u16)negative_mod(s)<<16)^(u16)negative_div(s);
        else if(kind==3)a=((u32)(u16)wrapped_mod(w)<<16)^(u16)negative_divisor(s);
        else a=volatile_mod(x);
        r=(r<<3)^(r>>5)^a;
    }
    return r;
}
'''
    (host / 'work.c').write_text(work)
    (project / 'work.c').write_bytes((host / 'work.c').read_bytes())
    # This dispatcher uses explicit fixed-width intermediates on host and MCU.
    header = '''#if defined(__SDCC) || defined(__C251__)
typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;
#else
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#endif
u32 stress_run(u8 kind,u8 seed);
'''
    (host / 'stress.h').write_text(header)
    (host / 'main.c').write_text('#include <stdio.h>\n#include "stress.h"\n'
        'int main(void) { unsigned k,s;for(k=0;k<5;k++)for(s=0;s<64;s++)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc, '-std=c99', '-O2', '-Wall', 'main.c', 'work.c', '-o', 'reference.exe'], host)
    values = {k:int(v) for k,v in (l.split() for l in user_stress.run([host / 'reference.exe'], host).splitlines())}
    board.write(root / 'reference.json', values)
    (project / 'stress.h').write_text(header)
    main = (user_stress.SOURCES.parent / 'machine-board.c').read_text().replace('"machine-board.h"', '"stress.h"')
    start, end = main.index('        x0 = '), main.index('        checksum = 0;')
    main = main[:start]+'        result=stress_run(kind,input); ticks=0; overflow=0;\n'+main[end:]
    main = main.replace('kind < 2', 'kind < 5').replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (project / 'board.c').write_text(main)
    files = ['board.c', 'work.c', 'stress.h']
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i,f in enumerate(files[:2])]
    board.write(project / 'mx251.json', dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=['u0','u1','@startup']))
    (project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(power2 NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE board.c work.c stress.h)\nmx251_firmware(app)\n')
    board.write(root / 'source.json', dict(probeSource=board.sha(source), workSha256=board.sha(project / 'work.c'),
        kinds=['promoted16','promoted32','signed','wrap-and-negative-divisor','volatile'],
        cases=320, innerInputsPerCase=4))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--gcc', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True)
    for c in sub.choices.values(): c.add_argument('--root', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.root, a.gcc)
    else: bitfield_fixture.capture(a.hex, a.root / 'capture', a.root / 'reference.json')
