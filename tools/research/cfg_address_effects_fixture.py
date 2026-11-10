"""Merged far addresses and polling loops called from real Timer0/1 ISRs."""
import argparse
import json
from pathlib import Path
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def expected(kind, seed):
    values = [(seed * 13 + i * 7) & 255 for i in range(12)]
    channel, pwm = seed & 3, (seed * 37) ^ 0x5a3
    if channel < 3:
        high, low = channel * 4 + 1, channel * 4
        values[high] = (values[high] & 0xcd) | ((pwm & 0x300) >> 4) | ((pwm & 0x400) >> 9)
        values[low] = pwm & 255
    values.append((seed | 1) & (0xfe if kind else 0xff))
    result = isr.expected(kind, seed)
    for value in values:
        result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    isr.create(output)
    (output/'call-boundary-isr.c').write_text('''#include "tail-boundary.h"
__sfr __at(0x89) TMOD;
__sfr __at(0xa8) IE;
__sbit __at(0x8c) TR0;
__sbit __at(0x8d) TF0;
#define REG(n) (*(volatile __xdata u8 *)(0x12000UL + (n)))
volatile __data u8 context_reserved_banks[24];
volatile __data u8 context_image[40];
volatile __data u8 context_bank, context_count, context_errors, context_mode;
volatile __data u8 context_dpx[6], context_index;
volatile __data u16 context_word;
void context_probe(void);
void context_step(u8 channel, u16 pwm);
void context_poll(void);

void context_irq(void) __interrupt(1)
{
    context_step(context_index, context_word);
    context_count = 1;
}
void context_leaf_irq(void) __interrupt(3)
{
    context_step(context_index, context_word);
    context_poll();
    context_count = 1;
}
u32 context_check(u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL;
    IE = context_mode ? 0x08 : 0x02;
    TR0 = 0; TF0 = 0; TMOD = 0x11;
    context_index = seed & 3;
    context_word = (u16)(seed * 37) ^ 0x5a3;
    for (i = 0; i < 12; ++i) REG(i) = (u8)(seed * 13 + i * 7);
    REG(16) = seed | 1;
    context_bank = (seed & 3U) << 3;
    context_count = 0; context_errors = 0;
    context_probe();
    IE = 0; TF0 = 0;
    for (i = 0; i < 3; ++i) if (context_dpx[i] != context_dpx[i+3]) ++context_errors;
    for (i = 0; i < 32; ++i) result = (result << 5) ^ result ^ context_image[i];
    result = (result << 5) ^ result ^ context_count;
    result = (result << 5) ^ result ^ context_errors;
    result ^= (u32)(context_image[32] ^ context_image[34]) << 24;
    result ^= (u32)(context_image[33] ^ context_image[35]) << 16;
    result ^= (u32)(context_image[36] ^ context_image[38]) << 8;
    result ^= context_image[37] ^ context_image[39];
    for (i = 0; i < 12; ++i) result = (result << 5) ^ result ^ REG(i);
    return (result << 5) ^ result ^ REG(16);
}
''', newline='\n')
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    helper = (tests/'shared-tails-callees.c').read_text().split('void tail_clock')[0]
    helper = helper[helper.index('void tail_pwm'):].replace('tail_pwm', 'context_step').replace('TREG(', 'REG(')
    helper = '#include "tail-boundary.h"\n#define REG(n) (*(volatile __xdata u8 *)(0x12000UL + (n)))\n' + helper
    helper += '''
void context_poll(void)
{
    while ((REG(16) & 1) == 0) {}
    REG(16) &= 0xfe;
}
'''
    (output/'isr-helper.c').write_text(helper, newline='\n')
    config = json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json', dict(files={name:board.sha(output/name) for name in config['files']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, expected))
