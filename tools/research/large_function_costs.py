"""Compare instruction categories in final MX251 images and Keil listings.

Categories describe emitted bytes, not removable overhead. In particular,
stack traffic includes arguments and call saves as well as spilled values.
"""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re

from call_layout import ihex, read_unit
from unified_sdk_matrix import pin, read, save


REG = re.compile(r'(?:a|acc|b|r\d+|wr\d+|dr\d+|ar\d+|dpl|dph|dpx|dptr|spx|sp|psw\d*|c)', re.I)


def category(text):
    parts = text.lower().split(None, 1)
    op, operands = parts[0], parts[1] if len(parts) > 1 else ''
    if op in ('push', 'pushw', 'pop') or re.search(r'\bspx\b', operands):
        return 'stack'
    if op in ('ecall', 'lcall', 'acall'):
        return 'call'
    if op.startswith('j') or op.endswith('jmp') or op in ('cjne', 'djnz'):
        return 'branch'
    if op in ('ret', 'reti', 'eret'):
        return 'return'
    if op in ('mov', 'movx', 'movc', 'movz', 'movs'):
        args = [x.strip() for x in operands.split(',')]
        if len(args) != 2:
            return 'other'
        if all(REG.fullmatch(x) for x in args):
            return 'registerMove'
        if REG.fullmatch(args[0]) and args[1].startswith('#'):
            return 'immediateMove'
        return 'memoryMove'
    return 'arithmeticAndBits'


def totals(instructions):
    counts, sizes = Counter(), Counter()
    for text, size in instructions:
        key = category(text)
        counts[key] += 1
        sizes[key] += size
    return dict(bytes=sum(sizes.values()), instructionCount=sum(counts.values()),
                categoryBytes=dict(sizes), categoryInstructions=dict(counts))


def keil_functions(root):
    functions, sources = defaultdict(list), []
    for path in sorted(root.glob('unit*.lst')):
        sources.append(pin(path))
        owner = None
        for line in path.read_text('latin1').splitlines():
            begin = re.search(r';\s+FUNCTION (\S+) \(BEGIN\)', line)
            if begin:
                owner = begin[1].upper()
            if ' (END)' in line and 'FUNCTION ' in line:
                owner = None
            match = re.match(r'^[0-9A-F]{6}\s+([0-9A-F]+)\s+(?:[RAC]\s+)?([A-Z]+\s+.*?)\s*$', line)
            if owner and match:
                functions[owner].append((match[2].split(';')[0].strip(), len(match[1])//2))
    return functions, sources


def analyze(candidate, references, output):
    attribution = candidate / 'function-attribution.json'
    rows = []
    for project in read(attribution)['rows']:
        pairs = [p for p in project['pairs'] if p['delta'] >= 100]
        if not pairs:
            continue
        root = candidate / 'projects' / project['id'] / 'mx251/.build/mx251/latest'
        memory = ihex(root / 'firmware.hex')
        mx, inputs = defaultdict(list), [pin(root / 'firmware.hex')]
        for unit in read(root / 'project.snapshot.json')['units']:
            if Path(unit['file']).suffix.lower() != '.c':
                continue
            data = read_unit(root, unit['name'], memory)
            inputs += data['inputs']
            for ins in data['instructions']:
                if ins.owner:
                    mx[ins.owner.lstrip('_').upper()].append((ins.source, len(ins.data)))
        keil, sources = keil_functions(references / 'projects' / project['id'] / 'keil')
        for pair in pairs:
            a, b = totals(mx[pair['name']]), totals(keil[pair['name']])
            assert a['bytes'] == pair['mx'], (project['id'], pair, a)
            notes = next(f for f in project['functions'] if f['name'].lstrip('_').upper() == pair['name'])
            rows.append(dict(project=project['id'], function=pair['name'], gap=pair['delta'],
                mx=a, keil=b, keilSegmentBytes=pair['keil'],
                keilListingMatchesSegment=b['bytes'] == pair['keil'],
                backend=notes['backend'], isr=notes['isr'],
                isrEntryExitBytes=notes['entryExitPushPopBytes'],
                diagnostics=notes['diagnostics'], inputs=inputs+sources))
    rows.sort(key=lambda r: -r['gap'])
    save(output, dict(status='OBSERVED_INSTRUCTION_COSTS', attribution=pin(attribution), rows=rows,
        scope='Matched functions with >=100 B gap; categories are costs, not potential savings. '
              'Keil listing/segment mismatch is explicit; g083 has historical invalid-format limitations.'))
    for row in rows[:15]:
        print(row['project'], row['function'], row['gap'],
              row['mx']['categoryBytes'], row['keil']['categoryBytes'], row['keilListingMatchesSegment'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'references', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    analyze(args.candidate, args.references, args.output)
