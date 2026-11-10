"""Real interrupts: pure peripheral bits and bits mixed with C memory access."""
import argparse
import json
from pathlib import Path
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    isr.create(output)
    path = output / 'call-boundary-isr.c'
    source = path.read_text()
    # The probe selects all four classic banks. Keep its snapshot and control
    # objects above 0x1f so recording one register cannot overwrite another.
    source = source.replace('volatile __data u8 context_image[40];',
                            'volatile __data u8 context_reserved_banks[24];\n'
                            'volatile __data u8 context_image[40];')
    # Timer0/1 are stopped; software sets TF0/TF1 to trigger one interrupt.
    # Exercise non-classic bit-addressable TMOD without starting either timer.
    bits = '''    __asm
        setb 0x89.7
        clr 0x89.4
        cpl 0x89.5
    __endasm;
'''
    start, end = source.index('void context_irq('), source.index('void context_leaf_irq(')
    source = source[:start] + 'void context_irq(void) __interrupt(1)\n{\n' + bits + '}\n\n' + source[end:]
    source = source.replace('    context_buffer[context_index] = context_value;',
                            bits + '    context_buffer[context_index] = context_value;')
    source = source.replace('    if (context_mode) {',
                            '    if (TMOD != 0xa1) ++context_errors;\n'
                            '    if (!context_mode && TMOD == 0xa1) context_count = 1;\n'
                            '    if (context_mode) {\n'
                            '        if (context_total != 0x12345678UL) ++context_errors;')
    source = source.replace('} else if (context_total != (0x12345678UL + 0x01234567UL)) ++context_errors;',
                            '} else if (context_total != 0x12345678UL) ++context_errors;')
    path.write_text(source, newline='\n')
    config = json.loads((output / 'mx251.json').read_text())
    board.write(output / 'source.json', dict(files={name: board.sha(output / name) for name in config['files']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('capture'); p.add_argument('--hex', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.output)
    else:
        board.capture(args.hex, args.output, lambda data: board.check(data, isr.expected))
