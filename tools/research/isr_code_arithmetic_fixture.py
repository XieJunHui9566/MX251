"""Timer0/1 context preservation with CODE lookup and native MUL/DIV."""
import argparse
import json
from pathlib import Path
import isr_object_fixture as objects
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def create(output):
    objects.create(output)
    values = [0, 1, 2, 127, 128, 255, 256, 257,
              1023, 4095, 32767, 32768, 65532, 65533, 65534, 65535]
    divisors = [1, 3, 255, 257]
    def table(kind, name, data):
        return 'const __code '+kind+' '+name+'[] = {'+','.join(str(v)+'UL' for v in data)+'};\n'
    declarations = '''volatile __edata u16 arithmetic_divisor, arithmetic_value;
volatile __edata u16 arithmetic_quotient, arithmetic_remainder;
volatile __edata u32 arithmetic_product;
volatile __data u8 arithmetic_byte_divisor, arithmetic_byte_q, arithmetic_byte_r;
'''
    declarations += table('u16', 'arithmetic_table', values)
    declarations += table('u16', 'arithmetic_divisors', divisors)
    declarations += table('u16', 'expected_quotient', [values[s & 15]//divisors[s >> 4] for s in range(64)])
    declarations += table('u16', 'expected_remainder', [values[s & 15]%divisors[s >> 4] for s in range(64)])
    declarations += table('u32', 'expected_product', [values[s & 15]*divisors[s >> 4] for s in range(64)])
    declarations += table('u8', 'expected_byte_q', [(s ^ 0xa5)//(1+(s & 7)) for s in range(64)])
    declarations += table('u8', 'expected_byte_r', [(s ^ 0xa5)%(1+(s & 7)) for s in range(64)])
    path = output/'call-boundary-isr.c'
    source = path.read_text()
    source = source.replace('void context_step(void);', 'void context_step(void);\n'+declarations)
    source = source.replace('    ++context_count;', '''    arithmetic_value = arithmetic_table[context_index];
    arithmetic_quotient = arithmetic_value / arithmetic_divisor;
    arithmetic_remainder = arithmetic_value % arithmetic_divisor;
    arithmetic_product = (u32)arithmetic_value * arithmetic_divisor;
    arithmetic_byte_q = context_value / arithmetic_byte_divisor;
    arithmetic_byte_r = context_value % arithmetic_byte_divisor;
    ++context_count;''')
    source = source.replace('    context_probe();', '''    arithmetic_divisor = arithmetic_divisors[seed >> 4];
    arithmetic_byte_divisor = 1 + (seed & 7);
    arithmetic_quotient = arithmetic_remainder = 0xa55a;
    arithmetic_product = 0xa55a5aa5UL;
    context_probe();
    if (arithmetic_value != arithmetic_table[seed & 15] ||
        arithmetic_quotient != expected_quotient[seed] ||
        arithmetic_remainder != expected_remainder[seed] ||
        arithmetic_product != expected_product[seed] ||
        arithmetic_byte_q != expected_byte_q[seed] ||
        arithmetic_byte_r != expected_byte_r[seed]) ++context_errors;''')
    path.write_text(source, newline='\n')
    config = json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json', dict(variant='isr-code-arithmetic',
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
