"""Translate C251 vector redirects into MX251 naked ISR entries.

The hardware interrupt frame is retained: a wrapper jumps to the original
destination vector. The compiler owns table placement and discovers all slots.
Other A251 programs require the assembler dialect adapter.
"""
import re


def translate(source, prefix='mx251_vector'):
    entries, jumps, labels, pending = [], [], {}, None
    for line in source.splitlines():
        text = line.split(';')[0].strip()
        if not text or text.startswith('//'):
            continue
        if text.upper() == 'END':
            break
        match = re.fullmatch(r'CSEG\s+AT\s+([0-9][0-9a-f]*)H', text, re.I)
        if match and pending is None:
            pending = int(match[1], 16)
            continue
        match = re.fullmatch(r'([A-Za-z_?$][\w?$]*):', text)
        if match:
            label = match[1].lower()
            if label in labels:
                return None
            labels[label] = len(jumps)
            continue
        match = re.fullmatch(r'L?JMP\s+([0-9][0-9a-f]*H|[A-Za-z_?$][\w?$]*)', text, re.I)
        if not match:
            return None
        if pending is not None:
            entries.append((pending, len(jumps)))
            pending = None
        jumps.append(match[1].lower())
    if not entries or pending is not None:
        return None
    resolved, slots = [], set()
    for address, index in entries:
        visited = set()
        while True:
            if index in visited or index >= len(jumps):
                return None
            visited.add(index)
            target = jumps[index]
            if re.fullmatch(r'[0-9][0-9a-f]*h', target):
                target = int(target[:-1], 16)
                break
            if target not in labels:
                return None
            index = labels[target]
        if address in slots or any(a < 3 or a > 3+8*255 or (a-3) % 8 for a in (address, target)):
            return None
        slots.add(address)
        resolved.append(((address-3)//8, target))
    result = ['/* C251 vector redirects; no software prologue or return. */']
    for slot, target in resolved:
        result += [f'void {prefix}_{slot}(void) __interrupt({slot}) __naked {{',
                   f'    __asm ejmp 0x{0xff0000+target:06x} __endasm;', '}']
    return '\n'.join(result)+'\n'
