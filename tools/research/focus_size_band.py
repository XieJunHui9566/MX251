"""Attribute a size-ratio band using existing final ASM/RST/HEX and Keil maps.

Reads completed builds only. Instruction and save-byte totals are observed
costs, not predicted savings. Keil function segments can also contain tables.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from call_layout import ihex, read_unit, sha


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def pin(path):
    return dict(path=str(path.resolve()), sha256=sha(path))


def functions(root, memory):
    result, inputs = [], []
    for unit in read(root / 'project.snapshot.json')['units']:
        if Path(unit['file']).suffix.lower() != '.c':
            continue
        data = read_unit(root, unit['name'], memory)
        inputs.extend(data['inputs'])
        instructions = defaultdict(list)
        for ins in data['instructions']:
            if ins.owner:
                instructions[ins.owner].append(ins)
        owner, diagnostics, body_bounds = None, defaultdict(list), defaultdict(dict)
        for number, line in enumerate(data['lines'], 1):
            begin = re.search(r'mx251-function begin schema=1 symbol=(\S+)', line)
            if begin:
                owner = begin[1]
            if owner:
                if re.search(r'wide (?:refusal|admission|division deferred)|'
                             r'mcs251 wide-mir (?:accepted|fallback)|'
                             r'mx251-(?:body-choice|leaf-isr-saves|leaf-isr-fallback)', line):
                    diagnostics[owner].append(line.strip().lstrip(';').strip())
                for kind in ('begin', 'end'):
                    if 'mx251-isr-body-' + kind in line:
                        body_bounds[owner][kind] = number
            if 'mx251-function end schema=1' in line:
                owner = None
        for name, code in instructions.items():
            notes = diagnostics[name]
            refusals = [re.search(r' reason=(\S+)', n)[1] for n in notes if n.startswith('wide refusal ')]
            choices = [re.search(r' selected=(\S+)', n)[1] for n in notes if n.startswith('mx251-body-choice ')]
            entry_skip = any('skip=entry-contract' in n for n in notes)
            bounds = body_bounds[name]
            push_pop = [ins for ins in code if re.match(r'(?:pushw?|pop)\s', ins.source)]
            saves = [ins for ins in push_pop if bounds and
                     (ins.line < bounds['begin'] or ins.line > bounds['end'])]
            result.append(dict(name=name, unit=unit['name'], bytes=sum(len(i.data) for i in code),
                               isr=data['flags'].get(name, {}).get('isr') == '1',
                               pushPopBytes=sum(len(i.data) for i in push_pop),
                               entryExitPushPopBytes=sum(len(i.data) for i in saves),
                               directCallSites=sum(bool(re.match(r'(?:e|l|a)call\s', i.source)) for i in code),
                               wideRefusal=refusals[-1] if refusals else None,
                               backend=choices[-1] if choices else ('entry-contract' if entry_skip else 'unreported'),
                               divisionDeferred=any(n.startswith('wide division deferred') for n in notes),
                               diagnostics=notes))
    return result, inputs


def project(row, candidate, references):
    key = row['id']
    root = candidate / 'projects' / key / 'mx251' / '.build/mx251/latest'
    firmware, mapfile = root / 'firmware.hex', root / 'linked.partial.map'
    keilmap = references / 'projects' / key / 'keil/FIRMWARE.MAP'
    memory = ihex(firmware)
    assert len(memory) == row['mx251Bytes'], key
    funcs, inputs = functions(root, memory)
    inputs.extend(pin(p) for p in (firmware, mapfile, keilmap, root / 'project.snapshot.json'))
    areas = {}
    for line in mapfile.read_text(encoding='latin1').splitlines():
        m = re.match(r'^(\S+)\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+=', line)
        if m:
            start, size = int(m[2], 16), int(m[3], 16)
            areas[m[1]] = dict(name=m[1], start=start, size=size,
                              payloadBytes=sum(start <= a < start + size for a in memory))
    segments = []
    for line in keilmap.read_text(encoding='latin1').splitlines():
        m = re.match(r'^([0-9A-F]+)H\s+([0-9A-F]+)H\s+([0-9A-F]+)H\s+\S+\s+\S+\s+CODE\s+(\S+)', line)
        if m:
            segments.append(dict(start=int(m[1], 16), end=int(m[2], 16), bytes=int(m[3], 16), name=m[4]))
    mx, keil, isr_names = Counter(), Counter(), set()
    for f in funcs:
        name = f['name'].lstrip('_').upper()
        mx[name] += f['bytes']
        if f['isr']:
            isr_names.add(name)
    for seg in segments:
        m = re.match(r'\?PR\?([^?]+)\?', seg['name'])
        if m:
            keil[m[1]] += seg['bytes']
    pairs = sorted([dict(name=n, mx=mx[n], keil=keil[n], delta=mx[n]-keil[n], isr=n in isr_names)
                    for n in mx.keys() & keil.keys()], key=lambda p: (-p['delta'], p['name']))
    return dict(id=key, name=row['name'], mxBytes=row['mx251Bytes'], keilBytes=row['keilBytes'],
                ratio=row['keilRatio'], gap=row['mx251Bytes']-row['keilBytes'],
                functions=funcs, pairs=pairs, areas=list(areas.values()), keilSegments=segments,
                mxApplicationInstructionBytes=sum(mx.values()), keilFunctionSegmentBytes=sum(keil.values()),
                mxStartupVectorBytes=sum(a['payloadBytes'] for n, a in areas.items()
                                         if n == 'HOME' or n.startswith('GS')), inputs=inputs)


def summarize(rows):
    reasons, projects, sizes = Counter(), defaultdict(set), Counter()
    isrs, deferred, core, leaf = [], [], [], []
    for row in rows:
        for f in row['functions']:
            site = dict(project=row['id'], **f)
            if f['wideRefusal']:
                reason = f['wideRefusal']
                reasons[reason] += 1
                projects[reason].add(row['id'])
                sizes[reason] += f['bytes']
            if f['isr']:
                isrs.append(site)
            if f['divisionDeferred']:
                deferred.append(site)
            if f['wideRefusal'] == 'ordered-asm-core-state-write':
                core.append(site)
            if any(n.startswith('mx251-leaf-isr-saves ') for n in f['diagnostics']):
                leaf.append(site)
    return dict(projects=len(rows), mxBytes=sum(r['mxBytes'] for r in rows),
                keilBytes=sum(r['keilBytes'] for r in rows),
                isrProjects=len({f['project'] for f in isrs}), isrFunctions=len(isrs),
                isrEntryExitPushPopBytes=sum(f['entryExitPushPopBytes'] for f in isrs),
                isrsWithCalls=sum(f['directCallSites'] > 0 for f in isrs),
                leafIsrOptimizationFunctions=len(leaf),
                matchedIsrNetGapBytes=sum(p['delta'] for r in rows for p in r['pairs'] if p['isr']),
                divisionDeferredProjects=len({f['project'] for f in deferred}), divisionDeferred=deferred,
                coreStateFallbackProjects=len({f['project'] for f in core}), coreStateFallbackFunctions=len(core),
                coreStateFallbackBodyBytes=sum(f['bytes'] for f in core),
                refusalReasons=[dict(reason=k, functions=v, projects=sorted(projects[k]), bodyBytes=sizes[k])
                                for k, v in reasons.most_common()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--references', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--minimum', type=int, default=130)
    parser.add_argument('--maximum', type=int, default=150)
    args = parser.parse_args()
    rows = [project(r, args.candidate, args.references) for r in read(args.comparison)['rows']]
    focus = [r for r in rows if args.minimum*r['keilBytes'] <= 100*r['mxBytes'] <= args.maximum*r['keilBytes']]
    output = dict(status='READ_ONLY_FINAL_IMAGE_ATTRIBUTION', comparison=pin(args.comparison),
                  bandPercent=[args.minimum, args.maximum],
                  measurement='MX application instruction bytes; Keil function-segment bytes. '
                              'PUSH/POP bytes are encoded bytes, not stack depth or removable savings. '
                              'Function segments may include tables; unmatched remainder includes constants and libraries.',
                  fullSummary=summarize(rows), focusSummary=summarize(focus), rows=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')
    for label, summary in [('full', output['fullSummary']), ('focus', output['focusSummary'])]:
        print(label, json.dumps({k: v for k, v in summary.items() if k not in ('divisionDeferred', 'refusalReasons')}, ensure_ascii=False))
    for row in focus:
        top = ', '.join(f"{p['name']} {p['mx']}/{p['keil']} ({p['delta']:+})" for p in row['pairs'][:3])
        print(f"{row['id']} {100*row['ratio']:.2f}% gap={row['gap']} {top}")


if __name__ == '__main__':
    main()
