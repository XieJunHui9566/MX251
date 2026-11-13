"""Packed PCON-bit ISR contexts and ordinary dead copies on real hardware."""
import argparse
import json
from pathlib import Path
import isr_stack_packed_fixture as packed
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    packed.create(output)
    path = output / 'call-boundary-isr.c'
    source = path.read_text()
    source = source.replace('        setb 0x89.7', '        clr 0x87.5\n        setb 0x89.7')
    source = source.replace('void context_clobber(void);',
        'void context_clobber(void);\nvoid register_work(void);\n'
        '#define copy_cell (*(volatile __xdata u8 *)0x12001UL)')
    source = source.replace('    context_probe();',
        '    copy_cell = seed ^ 0x5aU;\n'
        '    register_work();\n'
        '    if (copy_cell != ((seed ^ 0x5aU) & 0xfcU)) ++context_errors;\n'
        '    context_probe();')
    path.write_text(source, newline='\n')
    (output / 'register-work.c').write_text('''typedef unsigned char u8;
#define copy_cell (*(volatile __xdata u8 *)0x12001UL)
void register_work(void)
{
    copy_cell &= ~3;
    __asm clr 0x89.4 __endasm;
}
''', newline='\n')
    config = json.loads((output / 'mx251.json').read_text())
    config['files'].append('register-work.c')
    config['units'].append(dict(name='u7', file='register-work.c', backend='legacy',
                                stackAuto=True, defines=[]))
    config['link'].insert(-1, 'u7')
    board.write(output / 'mx251.json', config)
    path = output / 'CMakeLists.txt'
    path.write_text(path.read_text().replace('isr-helper.c', 'isr-helper.c register-work.c'), newline='\n')
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
