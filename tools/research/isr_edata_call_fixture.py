"""Timer0 call to an EDATA writer and PSW.F0 alias, with full context checks."""
import argparse
import json
from pathlib import Path
import isr_dpx_fixture as dpx
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    dpx.create(output, 'full')
    path = output/'isr-helper.c'
    source = path.read_text().replace('void context_step(void) {', '''
volatile __edata unsigned char helper_byte;
volatile __edata unsigned int helper_word;
volatile __edata u32 helper_total;
__sbit __at(0xd5) helper_f0;
void context_step(void) {
    ++helper_byte;
    helper_word += helper_byte;
    helper_total ^= (u32)helper_word;
    helper_f0 = 1;
''')
    path.write_text(source, newline='\n')
    path = output/'call-boundary-isr.c'
    source = path.read_text().replace('void context_step(void);', '''void context_step(void);
extern volatile __edata u8 helper_byte;
extern volatile __edata u16 helper_word;
extern volatile __edata u32 helper_total;''')
    source = source.replace('    context_probe();', '''    helper_byte = seed;
    helper_word = 0xffe0u + seed;
    helper_total = 0x12345678UL;
    context_probe();
    if (context_mode) {
        if (helper_byte != seed || helper_word != (u16)(0xffe0u + seed) ||
            helper_total != 0x12345678UL) ++context_errors;
    } else {
        if (helper_byte != seed + 1 || helper_word != (u16)(0xffe1u + 2 * seed) ||
            helper_total != (0x12345678UL ^ (u16)(0xffe1u + 2 * seed))) ++context_errors;
    }''')
    path.write_text(source, newline='\n')
    config = json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json', dict(variant='isr-edata-call',
                files={name: board.sha(output/name) for name in config['files']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, isr.expected))
