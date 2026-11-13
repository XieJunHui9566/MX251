"""Cross-unit array stores with a 16-bit index during Timer0/1 preemption."""
import argparse, json
from pathlib import Path
import isr_dpx_fixture as dpx
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    dpx.create(output, 'full')
    p = output/'call-boundary-isr.c'
    text = p.read_text().replace('context_mode,context_index,context_value;',
        'context_mode,context_value;\nvolatile __data u16 context_index;')
    text = text.replace('} else if (context_total != (0x12345678UL + 0x01234567UL)) ++context_errors;',
        '} else if (context_total != (0x12345678UL + 0x01234567UL) || context_buffer[context_index] != context_value) ++context_errors;')
    p.write_text(text, newline='\n')
    p = output/'isr-helper.c'
    text = p.read_text().replace('void context_step(void) {',
        'extern volatile __data unsigned int context_index;\n'
        'extern volatile __data unsigned char context_value;\n'
        'extern volatile __xdata unsigned char context_buffer[16];\n'
        'void context_step(void) { context_buffer[context_index]=context_value;')
    p.write_text(text, newline='\n')
    config = json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json', dict(variant='symbol-address', files={f:board.sha(output/f) for f in config['files']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    s = sub.add_parser('create'); s.add_argument('--output', type=Path, required=True)
    s = sub.add_parser('capture'); s.add_argument('--hex', type=Path, required=True); s.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    else: board.capture(a.hex, a.output, lambda data:board.check(data, isr.expected))
