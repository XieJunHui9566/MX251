"""IDATA RAM, signedness-only negation and ABI2/4 under banked interrupts."""
import argparse
import json
from pathlib import Path
import native_isr_fixture as native
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def expected_total(value, framed):
    total = native.expected_total(value, False)
    words = [value+257*i for i in range(64)]
    index = value & 31
    total ^= words[0]+words[63]+words[index]+words[index+1]
    for i in range(16):
        a = 0 if i == 0 else 0x7fffffff if i == 1 else value*0x12345+i
        b = 0 if i == 0 else 32767 if i == 1 else value*97+i
        total = ((total << 3) ^ (total >> 7) ^ (-a & 0xffffffff)) & 0xffffffff
        total = (total + a + (-b & 0xffff) + b) & 0xffffffff
    if framed:
        total = ((total ^ 0x56780000)+value) & 0xffffffff
    return total


def create(output):
    native.create(output)
    path = output/'call-boundary-isr.c'
    source = path.read_text()
    start, end = source.index('const __code u32 expected_0'), source.index('void context_irq(')
    tables = ''.join('const __code u32 expected_'+str(mode)+'[64]={'+','.join(
        hex(expected_total(seed ^ 0xa5, mode))+'UL' for seed in range(64))+'};\n' for mode in (0, 1))
    declarations = '''volatile __idata u16 near_words[64];
long negate32_2(u32 x) __reentrant __sdcccall(2);
u32 negate32_4(long x) __reentrant __sdcccall(4);
int negate16_4(u16 x) __reentrant __sdcccall(4);
u16 negate16_2(int x) __reentrant __sdcccall(2);
u16 read_generic(volatile u16 *p) __reentrant __sdcccall(4);
'''
    source = source[:start]+tables+declarations+source[end:]
    # Both ISR paths exercise the same calculation; one also has a local frame.
    source = source.replace('u8 i;u32 total=0x89abcdefUL;',
        'u8 i;u32 a;u16 b;volatile u16 __idata *p;u32 total=0x89abcdefUL;')
    anchor = 'for(i=0;i<9;i++)total=(total<<3)^(total>>7)^((u32)(context_value+i)*0x12345UL);'
    source = source.replace(anchor, anchor+'''
    for(i=0;i<64;i++)near_words[i]=context_value+257u*i;
    p=near_words+(context_value&31);
    total^=(u32)near_words[0]+near_words[63]+*p+read_generic(p+1);
    for(i=0;i<16;i++) {
        a=i==0?0:i==1?0x7fffffffUL:(u32)context_value*0x12345UL+i;
        b=i==0?0:i==1?32767u:(u16)context_value*97u+i;
        total=(total<<3)^(total>>7)^(u32)negate32_2(a);
        total+=negate32_4(-(long)a)+(u16)negate16_4(b)+negate16_2(-(int)b);
    }
''')
    path.write_text(source, newline='\n')
    path = output/'isr-helper.c'
    path.write_text(path.read_text()+'''
long negate32_2(u32 x) __reentrant __sdcccall(2) {long value=x;return -value;}
u32 negate32_4(long x) __reentrant __sdcccall(4) {u32 value=x;return -value;}
int negate16_4(u16 x) __reentrant __sdcccall(4) {int value=x;return -value;}
u16 negate16_2(int x) __reentrant __sdcccall(2) {u16 value=x;return -value;}
u16 read_generic(volatile u16 *p) __reentrant __sdcccall(4) {return *p;}
''', newline='\n')
    config = json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json', dict(files={name: board.sha(output/name) for name in config['files']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    q = sub.add_parser('create'); q.add_argument('--output', type=Path, required=True)
    q = sub.add_parser('capture')
    q.add_argument('--hex', type=Path, required=True); q.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, isr.expected))
