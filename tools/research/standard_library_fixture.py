"""Independent memory/string workload for the three refreshed ABI2 members."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


LENGTHS = [0, 1, 2, 7, 15, 31, 32, 63, 64, 127, 128, 254, 255, 256, 257, 319]


def expected(kind, seed):
    return 0x600d0000 | (kind << 8) | seed


def create(output, model):
    output.mkdir(parents=True, exist_ok=False)
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    begin, end = driver.index('        x0 ='), driver.index('        TR0 = 0; ticks')
    driver = driver[:begin]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = library_case(kind,input);
'''+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver, newline='\n')
    (output/'machine-board.h').write_text('typedef unsigned char u8;\ntypedef unsigned int u16;\ntypedef unsigned long u32;\nu32 library_case(u8,u8);\n', newline='\n')
    source = '''#include "machine-board.h"
#include <string.h>
static const u16 __code lengths[16] = {LENGTHS};
static const u8 __code constant[321] = {CONSTANT};
static u8 __edata internal[324];
static u8 __xdata external[324];
static u8 __data short_text[34];

u32 library_case(u8 kind, u8 seed)
{
    u16 n = lengths[seed & 15], i;
    int c = (seed & 48) == 0 ? 0 : (seed & 48) == 16 ? 0x80 : (seed & 48) == 32 ? -1 : 0x123;
    u8 *d = kind ? external : internal;
    u8 *s = kind ? internal : external;
    void *returned;
    for (i=0;i<324;++i) { d[i]=0x5a; s[i]=(u8)(i | 1); }
    returned=memset(d+1,c,n);
    if(returned != d+1) return 0xbad00001UL;
    for(i=0;i<324;++i)
        if(d[i] != ((i>0 && i<=n) ? (u8)c : 0x5a)) return 0xbad00002UL;
    returned=memcpy(d+1,constant,n);
    if(returned != d+1) return 0xbad00003UL;
    for(i=0;i<n;++i) if(d[i+1] != constant[i]) return 0xbad00004UL;
    if(d[0]!=0x5a || d[n+1]!=0x5a) return 0xbad00005UL;
    d[n+1]=0;
    if(strlen((char *)d+1)!=n) return 0xbad00006UL;
    returned=memcpy(d+1,s+2,n);
    if(returned != d+1) return 0xbad00007UL;
    for(i=0;i<n;++i) if(d[i+1]!=s[i+2]) return 0xbad00008UL;
    if(d[0]!=0x5a || d[n+1]!=0) return 0xbad00009UL;
    if(strlen((char *)d+1)!=n) return 0xbad0000aUL;
    if(strlen((const char *)constant)!=320UL) return 0xbad0000bUL;
    n=seed & 31;
    short_text[0]=0x5a; short_text[n+1]=0;
    returned=memcpy(short_text+1,d+1,n);
    if(returned!=short_text+1) return 0xbad0000cUL;
    for(i=0;i<n;++i) if(short_text[i+1]!=d[i+1]) return 0xbad0000dUL;
    returned=memset(short_text+1,0xff,n);
    if(returned!=short_text+1 || short_text[0]!=0x5a || short_text[n+1]!=0)
        return 0xbad0000eUL;
    for(i=0;i<n;++i) if(short_text[i+1]!=0xff) return 0xbad0000fUL;
    if(strlen((char *)short_text+1)!=n) return 0xbad00010UL;
    return 0x600d0000UL | ((u32)kind << 8) | seed;
}
'''.replace('LENGTHS', ','.join(map(str, LENGTHS))).replace('CONSTANT', ','.join(str((i & 255) | 1) for i in range(320))+',0')
    (output/'cases.c').write_text(source, encoding='ascii', newline='\n')
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32',
        model=model, optimization='speed', backend='ordinary-wide', explicitAbi=False,
        standard='sdcc11', runtime='standard', defines=[]))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(standard_library NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE machine-board.c cases.c machine-board.h)\nmx251_firmware(app)\n', newline='\n')
    board.write(output/'source.json', {p.name: board.sha(p) for p in output.iterdir() if p.is_file()})


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create')
    c.add_argument('--output', type=Path, required=True)
    c.add_argument('--model', choices=['small', 'large'], default='small')
    c = sub.add_parser('capture')
    c.add_argument('--hex', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('check')
    c.add_argument('capture', type=Path)
    a = p.parse_args()
    if a.command == 'create':
        create(a.output, a.model)
    elif a.command == 'capture':
        board.capture(a.hex, a.output, lambda data: board.check(data, expected))
    else:
        print(board.check(a.capture.read_bytes(), expected))
