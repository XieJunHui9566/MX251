"""Independent integer formatting and modular-negation board checks."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


VALUES = [0, 1, 9, 10, 99, 100, 255, 256, 32767, 32768, 65535,
          2147483647, 2147483648, 4294967295, 1000000000, 123456789]

SOURCE = r'''
#include <stdint.h>
#include <stdio.h>
static const uint32_t __code values[16]={0,1,9,10,99,100,255,256,32767,32768,
    65535,2147483647UL,2147483648UL,4294967295UL,1000000000UL,123456789UL};
static char __xdata buffer[128];
static uint32_t digest;
static void mix(uint16_t value) { digest=((digest<<5)^digest)^value; }
int putchar(int value) { mix((unsigned char)value); return (unsigned char)value; }
uint16_t loop_negate(uint16_t word, unsigned char byte, unsigned char count)
{
    uint16_t sum=0;
    do {
        word=(uint16_t)(0u-word);
        byte=(unsigned char)(0u-byte);
        sum=(uint16_t)(sum+word+byte);
        word=(uint16_t)(word+count);
        byte=(unsigned char)(byte+count);
    } while (--count);
    return sum;
}
uint32_t format16(unsigned char seed)
{
    uint16_t value=(uint16_t)values[seed&15] ^ ((uint16_t)(seed>>4)*257u);
    int signed_value=(int)value;
    unsigned char i;
    int count;
    digest=0x12345678UL;
    count=printf("%d|%u|%06o|%04x|%X|%3c|%%",signed_value,value,value,value,value,(int)(seed&3));
    mix((uint16_t)count);
    count=sprintf(buffer,"%06d|%5u|%bd|%bu",signed_value,value,(int)(signed char)value,(unsigned int)(unsigned char)value);
    mix((uint16_t)count);
    for(i=0;i<=(unsigned char)count;++i) mix((unsigned char)buffer[i]);
    return digest;
}
uint32_t format32(unsigned char seed)
{
    uint32_t value=values[seed&15] ^ ((uint32_t)(seed>>4)*0x10001UL);
    int count;
    unsigned char i;
    digest=0x87654321UL;
    count=printf("%ld|%lu|%011lo|%08lx|%lX",(long)value,value,value,value,value);
    mix((uint16_t)count);
    count=sprintf(buffer,"%011ld|%10lu",(long)value,value);
    mix((uint16_t)count);
    for(i=0;i<=(unsigned char)count;++i) mix((unsigned char)buffer[i]);
    mix(loop_negate((uint16_t)value,seed,(seed&15)+1));
    return digest;
}
'''


def expected(kind, seed):
    def signed(value, bits):
        return value-(1 << bits) if value & (1 << (bits-1)) else value

    if kind == 0:
        value = (VALUES[seed & 15] & 65535) ^ ((seed >> 4)*257)
        sv = signed(value, 16)
        printed = f'{sv}|{value}|{value:06o}|{value:04x}|{value:X}|  {chr(seed & 3)}|%'
        buffered = f'{sv:06d}|{value:5d}|{signed(value & 255,8)}|{value & 255}'
        result = 0x12345678
    else:
        value = VALUES[seed & 15] ^ ((seed >> 4)*0x10001)
        sv = signed(value, 32)
        printed = f'{sv}|{value}|{value:011o}|{value:08x}|{value:X}'
        buffered = f'{sv:011d}|{value:10d}'
        result = 0x87654321
    stream = [*printed.encode(), len(printed), len(buffered), *buffered.encode(), 0]
    if kind:
        word, byte, total = value & 65535, seed, 0
        for count in range((seed & 15)+1, 0, -1):
            word, byte = -word & 65535, -byte & 255
            total = (total+word+byte) & 65535
            word, byte = (word+count) & 65535, (byte+count) & 255
        stream.append(total)
    for value in stream:
        result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result


def create(output, model):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    (output/'format.c').write_text(SOURCE, encoding='ascii', newline='\n')
    shutil.copy2(tests/'machine-board.h', output/'machine-board.h')
    text = (tests/'machine-board.c').read_text()
    text = text.replace('static u8 checksum;', 'u32 format16(u8);\nu32 format32(u8);\nstatic u8 checksum;')
    start, end = text.index('        x0 ='), text.index('        TH0 =')
    text = text[:start]+text[end:]
    start, end = text.index('        if (kind == 0)'), text.index('        TR0 = 0; ticks')
    text = text[:start]+'        result = kind ? format32(input) : format16(input);\n'+text[end:]
    text = text.replace('ticks, x0, y0, x1, y1, x2, y2', 'ticks')
    (output/'board.c').write_text(text, encoding='ascii', newline='\n')
    files = ['format.c', 'board.c', 'machine-board.h']
    units = [dict(name='u'+str(i), file=f, backend='ordinary-wide', stackAuto=True, defines=[])
             for i, f in enumerate(files[:2])]
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model=model,
        optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
        runtime='standard', defines=[], files=files, units=units, archives=[], link=['u0','u1','@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(loop_format NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n', encoding='ascii')
    board.write(output/'source.json', dict(model=model, files={f:board.sha(output/f) for f in files}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c.add_argument('--model', choices=['small','large'], default='small')
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('check'); c.add_argument('uart', type=Path)
    a = p.parse_args()
    if a.command == 'create': create(a.output, a.model)
    elif a.command == 'capture': board.capture(a.hex, a.output, lambda data:board.check(data, expected))
    else: print(board.check(a.uart.read_bytes(), expected))
