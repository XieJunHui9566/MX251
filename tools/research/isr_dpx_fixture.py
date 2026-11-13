"""Hardware ISR coverage: no DPX, full DPX, low-only and high-only writes."""
import argparse,json
from pathlib import Path
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output,variant):
    isr.create(output)
    path=output/'call-boundary-isr.c';source=path.read_text()
    source=source.replace('volatile __data u8 context_image[40];',
        'volatile __data u8 context_reserved_banks[24];\nvolatile __data u8 context_image[40];')
    if variant=='full':
        source=source.replace('    if (context_fixed & 1) context_fixed &= 0xfe;',
            '    if (context_fixed & 1) context_fixed &= 0xfe;\n'
            '    *(volatile __xdata u8 *)0x12001UL = 0x51;\n'
            '    *(volatile __xdata u8 *)0x12002UL = 0xa2;')
        source=source.replace('    if (context_mode) {','    if (context_mode) {\n'
            '        if (*(volatile __xdata u8 *)0x12001UL != 0x51 ||\n'
            '            *(volatile __xdata u8 *)0x12002UL != 0xa2) ++context_errors;')
    else:
        source=source.replace('void context_leaf_irq(void)', '__sfr __at(0x84) context_high;\nvoid context_leaf_irq(void)')
        source=source.replace('    context_buffer[context_index] = context_value;\n'
            '    if (context_fixed & 1) context_fixed &= 0xfe;', '    context_high = 0x35;')
        source=source.replace('        if (context_buffer[context_index] != context_value || context_fixed != 0x92) ++context_errors;',
            '        if (context_buffer[context_index] != 0 || context_fixed != 0x93) ++context_errors;')
        helper=output/'isr-helper.c'
        helper.write_text(helper.read_text().replace('void context_step(void) {',
            '__sfr __at(0x82) context_low;\n__sfr __at(0x83) context_mid;\n'
            'void context_step(void) { context_low=0x12; context_mid=0x34;'),newline='\n')
    path.write_text(source,newline='\n')
    config=json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json',dict(variant=variant,files={name:board.sha(output/name) for name in config['files']}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True);c.add_argument('--variant',choices=['full','split'],required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output,a.variant)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,isr.expected))
