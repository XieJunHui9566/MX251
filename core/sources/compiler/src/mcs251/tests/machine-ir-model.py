"""Independent execution model for machine IR differential tests.

Models GPRs, caller stack, direct-access order, CY/Z and typed-call effects.
It does not import optimizer implementation or effect tables.
"""
import random
import re

def execute(text, name, seed):
    instructions, labels = [], {}
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if not line:
            continue
        if line.endswith(':'):
            labels[line[:-1]] = len(instructions)
        else:
            op, *args = line.split(None, 1)
            instructions.append((op, args[0].split(',') if args else []))
    randomizer = random.Random(seed)
    regs = bytearray(randomizer.randrange(256) for _ in range(32))
    direct = {"_data": randomizer.randrange(256)}
    events = []
    memory = bytearray(8192)
    for i in range(0x1000 - 16, 0x1001):
        memory[i] = randomizer.randrange(256)
    sp = 0x1000
    carry = zero = False
    pc = labels[name]

    def register(s):
        if s.startswith('ar'): s = s[1:]
        if s in direct: return 0, 1
        if s == 'a':
            return 11, 1
        if s == 'b':
            return 10, 1
        match = re.fullmatch(r'(w|d)?r(\d+)', s)
        assert match, s
        return int(match[2]), {'w': 2, 'd': 4, None: 1}[match[1]]

    def immediate(s):
        s = re.sub(r'_data([0-2])', lambda m: str(0x231234 + int(m[1]) * 0x12345), s)
        assert re.fullmatch(r'[0-9a-fx()+> ]+', s), s
        return eval(s, {'__builtins__': {}}, {})

    def address(s):
        assert s.startswith('@spx'), s
        return sp + (int(s[4:], 0) if s[4:] else 0)

    def read(s, width=None):
        if s in direct:
            events.append(('read', s, direct[s]))
            return direct[s]
        if s == 'spx':
            return sp
        if s.startswith('#'):
            return immediate(s[1:])
        if s.startswith('@'):
            assert width
            at = address(s)
            return int.from_bytes(memory[at:at + width], 'big')
        first, width = register(s)
        return int.from_bytes(regs[first:first + width], 'big')

    def write(s, value, width=None):
        nonlocal sp
        if s in direct:
            direct[s] = value & 255
            events.append(('write', s, direct[s]))
            return
        if s == 'spx':
            sp = value
            return
        if s.startswith('@'):
            assert width
            at = address(s)
            memory[at:at + width] = (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'big')
            return
        first, width = register(s)
        regs[first:first + width] = (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'big')

    for _ in range(5000):
        op, args = instructions[pc]
        pc += 1
        if op == 'eret':
            return bytes(regs), bytes(memory[0x1000 - 16:0x1001]), sp - 3, events, carry, zero
        if op == 'ecall':
            assert args == ['_known']
            value = read('wr4') ^ 0x1357
            write('wr0', value + 17); write('wr4', value)
            carry = False; zero = value == 0
            continue
        if op == 'clr' and args == ['c']:
            carry = False
            continue
        if op == 'nop':
            continue
        if op in ('mul', 'div'):
            if args == ['ab']:
                left, right = read('a'), read('b')
                assert op != 'div' or right
                high, low = ((left * right) >> 8, (left * right) & 255) if op == 'mul' else divmod(left, right)[::-1]
                write('b', high); write('a', low)
                zero = (left * right == 0) if op == 'mul' else low == 0
            else:
                first, width = register(args[0])
                left, right = read(args[0]), read(args[1])
                assert op != 'div' or right
                value = left * right if op == 'mul' else ((left % right) << (8 * width)) | (left // right)
                at = first // (2 * width) * (2 * width)
                regs[at:at+2*width] = value.to_bytes(2*width, 'big')
                zero = value == 0 if op == 'mul' else left // right == 0
            carry = False
            continue
        if op in ('ejmp', 'sjmp', 'jc', 'jnc', 'je', 'jne'):
            take = {'ejmp': True, 'sjmp': True, 'jc': carry, 'jnc': not carry,
                    'je': zero, 'jne': not zero}[op]
            if take:
                pc = labels[args[0]]
            continue
        a = args[0]
        if op in ('push', 'pushw'):
            width = 2 if op == 'pushw' else register(a)[1]
            memory[sp + 1:sp + 1 + width] = (read(a) & ((1 << (width * 8)) - 1)).to_bytes(width, 'big')
            sp += width
            continue
        if op == 'pop':
            width = register(a)[1]
            write(a, int.from_bytes(memory[sp - width + 1:sp + 1], 'big'))
            sp -= width
            continue
        b = args[1]
        width = register(b)[1] if a.startswith('@') else 4 if a == 'spx' else register(a)[1]
        value = read(b, width)
        if op in ('mov', 'movz'):
            write(a, value, width)
            continue
        old = read(a, width)
        if op in ('cmp', 'cjne'):
            carry, zero = old < value, old == value
            if op == 'cjne' and not zero: pc = labels[args[2]]
            continue
        result = {'add': lambda: old + value, 'sub': lambda: old - value,
                  'anl': lambda: old & value, 'orl': lambda: old | value,
                  'xrl': lambda: old ^ value, 'inc': lambda: old + value}[op]()
        if op in ('add', 'sub'):
            carry = result < 0 or result >= 1 << (width * 8)
        zero = (result & ((1 << (width * 8)) - 1)) == 0
        write(a, result, width)
    raise AssertionError('non-terminating transformed function')

