"""Real Timer0/1 preemption with EDATA scalar writes and indexed XDATA stores."""
import argparse
import json
from pathlib import Path
import isr_dpx_fixture as dpx
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    dpx.create(output, 'full')
    path = output/'call-boundary-isr.c'
    source = path.read_text()
    source = source.replace('void context_step(void);', '''void context_step(void);
volatile __edata u8 object_count;
volatile __edata u16 object_word;
volatile __edata u32 object_total;''')
    source = source.replace('    ++context_count;', '''    ++object_count;
    object_word += context_value;
    object_total ^= (u32)object_word;
    ++context_count;''')
    source = source.replace('    context_probe();', '''    object_count = 0x7f;
    object_word = 0xff80u + seed;
    object_total = 0x12345678UL;
    context_probe();
    if (object_count != 0x80 || object_word != (u16)(0xff80u + seed + context_value) ||
        object_total != (0x12345678UL ^ (u16)(0xff80u + seed + context_value))) ++context_errors;''')
    path.write_text(source, newline='\n')
    config = json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json', dict(variant='isr-object',
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
