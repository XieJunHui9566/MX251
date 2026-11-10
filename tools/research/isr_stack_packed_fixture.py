"""Real interrupts with word arguments, peripheral bits and local stack frames."""
import argparse
import json
from pathlib import Path
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    isr.create(output)
    path = output / 'call-boundary-isr.c'
    source = path.read_text()
    source = source.replace('volatile __data u8 context_image[40];',
        'volatile __data u8 context_reserved_banks[24];\n'
        'volatile __data u8 context_image[40];')
    start, end = source.index('void context_irq('), source.index('u32 context_check(')
    source = source[:start] + '''u32 context_sum(u8 first, u16 word, u32 value);

void context_irq(void) __interrupt(1)
{
    __asm
        setb 0x89.7
        clr 0x89.4
        cpl 0x89.5
    __endasm;
    context_total = context_sum(context_value, 0x1234u, 0x89abcdefUL);
    ++context_count;
}

void context_leaf_irq(void) __interrupt(3)
{
    volatile u8 guard[9];
    u8 i;
    for (i = 0; i < 9; ++i) guard[i] = i ^ context_value;
    __asm
        setb 0x89.7
        clr 0x89.4
        cpl 0x89.5
    __endasm;
    context_total = context_sum(context_value, 0x5678u, 0xfedcba98UL);
    context_clobber();
    for (i = 0; i < 9; ++i)
        if (guard[i] != (i ^ context_value)) ++context_errors;
    ++context_count;
}

''' + source[end:]
    source = source.replace('    if (context_mode) {\n'
        '        if (context_buffer[context_index] != context_value || context_fixed != 0x92) ++context_errors;\n'
        '    } else if (context_total != (0x12345678UL + 0x01234567UL)) ++context_errors;',
        '    if (TMOD != 0xa1) ++context_errors;\n'
        '    if (context_mode) {\n'
        '        if (context_total != ((0xfedcba98UL ^ 0x56780000UL) + context_value)) ++context_errors;\n'
        '    } else if (context_total != ((0x89abcdefUL ^ 0x12340000UL) + context_value)) ++context_errors;')
    path.write_text(source, newline='\n')
    (output / 'isr-helper.c').write_text('''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u32 context_sum(u8 first, u16 word, u32 value)
{
    return (value ^ ((u32)word << 16)) + first;
}
''', newline='\n')
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
