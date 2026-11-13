"""Translate C251 numeric vector redirects into MX251 naked ISR entries.

The hardware interrupt frame is retained: a wrapper jumps to the original
destination vector. The compiler owns table placement and discovers all slots.
Other A251 programs require the assembler dialect adapter.
"""
import re


def translate(source, prefix='mx251_vector'):
    entries, address = [], None
    for line in source.splitlines():
        text = line.split(';')[0].strip()
        if not text or text.startswith('//') or text.upper() == 'END':
            continue
        match = re.fullmatch(r'CSEG\s+AT\s+([0-9a-f]+)H', text, re.I)
        if match and address is None:
            address = int(match[1], 16)
            continue
        match = re.fullmatch(r'L?JMP\s+([0-9a-f]+)H', text, re.I)
        if not match or address is None:
            return None
        target = int(match[1], 16)
        if any(a < 3 or a > 3+8*255 or (a-3) % 8 for a in (address, target)):
            return None
        entries.append(((address-3)//8, target))
        address = None
    if not entries or address is not None:
        return None
    result = ['/* C251 vector redirects; no software prologue or return. */']
    for slot, target in entries:
        result += [f'void {prefix}_{slot}(void) __interrupt({slot}) __naked {{',
                   f'    __asm ejmp 0x{0xff0000+target:06x} __endasm;', '}']
    return '\n'.join(result)+'\n'
