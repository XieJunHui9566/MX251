"""Board checks and timing for mixed integer printf/sprintf conversions."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board


VALUES = [0, 1, 9, 10, 99, 100, 255, 256, 65535, 65536,
          0x7fffffff, 0x80000000, 0xffffffff]
for power in range(3, 10):
    VALUES.extend([10**power-1, 10**power, 10**power+1])
while len(VALUES) < 64:
    VALUES.append((len(VALUES)*0x1020304+0x89abcdef) & 0xffffffff)


def expected(kind, seed, bits=32):
    value = VALUES[seed]
    low = value & 65535
    signed = value if value < 0x80000000 else value-0x100000000
    short = low if low < 32768 else low-65536
    if bits == 16:
        text = f'{low:04x} {low:06o}' if kind else f'value={low} {short} %'
    elif kind:
        text = f'{value:08x} {value:011o} {value:08X} {low:04x} {low:06o} '+chr(seed)
    else:
        text = f'{value} {signed} {low} {short}'
    result = len(text)
    for c in text:
        result = (result*33+ord(c)) & 0xffffffff
    return result


def create(output, runtime, bits=32):
    output.mkdir(parents=True, exist_ok=False)
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    begin = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', begin)
    driver = driver[:begin]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = format_case(kind,input);
'''+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;\nu32 format_case(u8,u8);\n')
    source = '''#include "machine-board.h"
#include <stdio.h>
static const u32 __code values[64]={VALUES};
static char text[64];
static u32 stream_hash;
static u16 stream_count;
int putchar(int c)
{
    stream_hash=stream_hash*33UL+(u8)c;
    ++stream_count;
    return c;
}
u32 format_case(u8 kind,u8 seed)
{
    u32 value=values[seed], hash;
    int count, printed;
    u8 i;
    if(kind) count=sprintf(text,"%08lx %011lo %08lX %04x %06o %c",value,value,value,(u16)value,(u16)value,(int)seed);
    else count=sprintf(text,"%lu %ld %u %d",value,(long)value,(u16)value,(int)value);
    hash=(u16)count;
    for(i=0;i<count;++i) hash=hash*33UL+(u8)text[i];
    if(text[count]) return 0xbad00001UL;
    stream_hash=(u16)count; stream_count=0;
    if(kind) printed=printf("%08lx %011lo %08lX %04x %06o %c",value,value,value,(u16)value,(u16)value,(int)seed);
    else printed=printf("%lu %ld %u %d",value,(long)value,(u16)value,(int)value);
    if(printed!=count || stream_count!=count || stream_hash!=hash) return 0xbad00002UL;
    return hash;
}
'''.replace('VALUES', ','.join(f'0x{v:08x}UL' for v in VALUES))
    if bits == 16:
        source = source.replace('"%08lx %011lo %08lX %04x %06o %c",value,value,value,(u16)value,(u16)value,(int)seed',
                                '"%04x %06o",(u16)value,(u16)value')
        source = source.replace('"%lu %ld %u %d",value,(long)value,(u16)value,(int)value',
                                '"value=%u %d %%",(u16)value,(int)value')
        source = source.replace('    u8 i;\n', '''    u8 i;
    text[0]='x';
    if(sprintf(text,"")!=0 || text[0] || printf("")!=0) return 0xbad00003UL;
''')
    (output/'cases.c').write_text(source, encoding='ascii')
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32',
        model='small', optimization='speed', backend='ordinary-wide', explicitAbi=False,
        standard='sdcc11', runtime=runtime, defines=[]))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(integer_format_divmod NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE machine-board.c cases.c machine-board.h)\nmx251_firmware(app)\n')
    board.write(output/'source.json', {p.name: board.sha(p) for p in output.iterdir() if p.is_file()})


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c.add_argument('--runtime', choices=['standard', 'native-int-mdu32'], default='native-int-mdu32')
    c.add_argument('--bits', type=int, choices=[16, 32], default=32)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    c.add_argument('--bits', type=int, choices=[16, 32], default=32)
    a = p.parse_args()
    if a.command == 'create':
        create(a.output, a.runtime, a.bits)
    else:
        board.capture(a.hex, a.output, lambda data: board.check(data, lambda k, s: expected(k, s, a.bits)))
