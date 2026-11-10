"""Direct SFR bit consumers under real Timer0/1 preemption in four banks."""
import argparse
import json
from pathlib import Path
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def expected(kind, seed):
    return isr.expected(kind, seed) ^ ((seed * 13 + 0x35) & 255)


def create(output):
    isr.create(output)
    source = '''#include "tail-boundary.h"
__sfr __at(0x89) TMOD;
__sfr __at(0xa8) IE;
__sfr __at(0x8c) probe_byte;
__sbit __at(0x8c) TR0;
__sbit __at(0x8d) TF0;
volatile __data u8 context_reserved_banks[24];
volatile __data u8 context_image[40];
volatile __data u8 context_bank, context_count, context_errors, context_mode;
volatile __data u8 context_dpx[6];
volatile __data u32 context_total;
void context_probe(void);
'''
    source += '\n'.join(f'volatile __bit observed_{b};' for b in range(8)) + '\n'
    source += 'void context_irq(void) __interrupt(1)\n{\n'
    source += '\n'.join(f'    observed_{b} = (probe_byte >> {b}) & 1;' for b in range(8))
    source += '\n    context_count = 1;\n}\n'
    source += 'void context_leaf_irq(void) __interrupt(3)\n{\n'
    source += '\n'.join(f'    if (probe_byte & {1<<b}) observed_{b} = 1; else observed_{b} = 0;' for b in range(8))
    source += '''
    context_count = 1;
}
u32 context_check(u8 seed)
{
    u8 i, observed;
    u32 result = 0x13579bdfUL;
    IE = context_mode ? 0x08 : 0x02;
    TR0 = 0; TF0 = 0; TMOD = 0x11;
    probe_byte = seed * 13U + 0x35U;
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
'''
    source += '    observed = ' + ' | '.join(f'(observed_{b} ? {1<<b}U : 0U)' for b in range(8)) + ';\n'
    source += '    return result ^ observed;\n}\n'
    (output/'call-boundary-isr.c').write_text(source, newline='\n')
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
