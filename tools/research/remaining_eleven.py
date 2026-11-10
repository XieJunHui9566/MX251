"""Attribute projects above a size threshold from existing build artifacts only."""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re

from focus_size_band import pin, project, read, summarize
from call_layout import ihex, read_unit


def canonical(name):
    # The ABI4 linker spelling is not part of the C function's identity.
    return re.sub(r'^_+m251r4_', '', name).lstrip('_').upper()


def enrich(row, candidate, references):
    result = project(row, candidate, references)
    mx, keil, isrs = Counter(), Counter(), set()
    for f in result['functions']:
        name = canonical(f['name'])
        f['cName'] = name
        mx[name] += f['bytes']
        if f['isr']:
            isrs.add(name)
    for seg in result['keilSegments']:
        m = re.match(r'\?PR\?([^?]+)\?', seg['name'])
        if m:
            keil[m[1]] += seg['bytes']
    result['pairs'] = sorted(
        [dict(name=n, mx=mx[n], keil=keil[n], delta=mx[n]-keil[n], isr=n in isrs)
         for n in mx.keys() & keil.keys()], key=lambda p: (-p['delta'], p['name']))
    result['mxUnpairedFunctions'] = {n: mx[n] for n in mx.keys() - keil.keys()}
    result['keilUnpairedFunctions'] = {n: keil[n] for n in keil.keys() - mx.keys()}
    result['bytesTo120'] = max(0, row['mx251Bytes'] - 6*row['keilBytes']//5)
    result['bytesTo110'] = max(0, row['mx251Bytes'] - 11*row['keilBytes']//10)
    result['mxOutsideFunctionInstructions'] = row['mx251Bytes'] - sum(mx.values())
    result['keilOutsideFunctionSegments'] = row['keilBytes'] - sum(keil.values())
    result['originalSourceIssue'] = row['originalSourceIssue']
    result['stcBytes'] = row['stcBytes']
    result['stcStatus'] = row['stcStatus']
    root = candidate / 'projects' / row['id'] / 'mx251/.build/mx251/latest'
    reference = references / 'projects' / row['id'] / 'keil'
    # Include HCONST and CONST: these bytes are present in the final image too.
    segments = []
    for line in (reference / 'FIRMWARE.MAP').read_text(encoding='latin1').splitlines():
        m = re.match(r'^([0-9A-F]+)H\s+([0-9A-F]+)H\s+([0-9A-F]+)H\s+\S+\s+\S+\s+(CODE|HCONST|CONST)\s+(\S+)', line)
        if not m:
            continue
        name = m[5]
        kind = ('startupVectors' if name.startswith(('?PR?IV?', '?CO?start251', '?C_C51STARTUP', '?CO?vectors?', '?CO?isr?'))
                else 'application' if name.startswith('?PR?')
                else 'libraries' if name == '?C?LIB_CODE'
                else 'constants')
        segments.append(dict(start=int(m[1], 16), end=int(m[2], 16), bytes=int(m[3], 16),
                             memoryClass=m[4], name=name, category=kind))
    result['keilAllImageSegments'] = segments
    kp = Counter()
    for s in segments:
        kp[s['category']] += s['bytes']
    mp = dict(application=result['mxApplicationInstructionBytes'],
              csegOutsideFunctions=next(a['payloadBytes'] for a in result['areas'] if a['name'] == 'CSEG') - result['mxApplicationInstructionBytes'],
              constants=sum(a['payloadBytes'] for a in result['areas'] if a['name'] in ('CONST', 'XINIT')),
              startupVectors=result['mxStartupVectorBytes'])
    result['partition'] = dict(mx=mp, keil=dict(kp),
                               mxUnattributed=row['mx251Bytes']-sum(mp.values()),
                               keilUnattributed=row['keilBytes']-sum(kp.values()))
    assert result['partition']['mxUnattributed'] == result['partition']['keilUnattributed'] == 0, row['id']
    result['keilFunctionSegmentBytes'] = kp['application']
    result['keilOutsideFunctionSegments'] = row['keilBytes'] - kp['application']
    result['pairs'] = [p for p in result['pairs'] if p['name'] != 'IV']
    result['keilUnpairedFunctions'].pop('IV', None)
    memory = ihex(root / 'firmware.hex')
    opcode_bytes = {}
    for unit in read(root / 'project.snapshot.json')['units']:
        if Path(unit['file']).suffix.lower() != '.c':
            result['inputs'].append(pin(root / unit['file']))
            continue
        data = read_unit(root, unit['name'], memory)
        for ins in data['instructions']:
            if ins.owner:
                opcode_bytes.setdefault(ins.owner, Counter())[ins.source.split()[0]] += len(ins.data)
    result['vectorDefaultRetiCount'] = 0
    for path in root.glob('u[0-9]*.asm'):
        if not re.fullmatch(r'u\d+\.asm', path.name):
            continue
        text = path.read_text(encoding='latin1')
        vector = re.search(r'__interrupt_vect:(.*?)__sdcc_mcs251_reset_trampoline::', text, re.S)
        if vector:
            result['vectorDefaultRetiCount'] += len(re.findall(r'^\s*reti\s*$', vector[1], re.M))
    keil_functions = {}
    for path in sorted(reference.glob('unit*.lst')):
        result['inputs'].append(pin(path))
        text = path.read_text(encoding='latin1')
        for m in re.finditer(r';\s+FUNCTION (\S+) \(BEGIN\)(.*?);\s+FUNCTION \1 \(END\)', text, re.S):
            opcodes = Counter()
            for line in m[2].splitlines():
                ins = re.match(r'^[0-9A-F]{6}\s+([0-9A-F]+)\s+(?:[RE]\s+)?([A-Z]+)\s', line)
                if ins:
                    opcodes[ins[2].lower()] += len(ins[1])//2
            keil_functions[m[1].upper()] = dict(instructionBytesByMnemonic=dict(opcodes),
                                               pushPopBytes=opcodes['push']+opcodes['pop'])
    for f in result['functions']:
        f['instructionBytesByMnemonic'] = dict(opcode_bytes.get(f['name'], {}))
        f['emittedBackend'] = f['backend'] if f['backend'] != 'unreported' else (
            'wide' if any(n.startswith('mcs251 wide-mir accepted:') for n in f['diagnostics']) else 'legacy')
        f['keilListing'] = keil_functions.get(f['cName'])
    result['runtimeReports'] = {}
    for name in ('format-runtime.json', 'startup-runtime.json', 'selective-leaf-abi.json'):
        path = root / name
        if path.exists():
            report = read(path)
            result['runtimeReports'][name] = {k: report[k] for k in ('status', 'applied', 'reason') if k in report}
            result['inputs'].append(pin(path))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--references', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--threshold', type=int, default=120)
    args = parser.parse_args()
    comparison = read(args.comparison)
    rows = [enrich(r, args.candidate, args.references) for r in comparison['rows']
            if 100*r['mx251Bytes'] > args.threshold*r['keilBytes']]
    result = dict(status='READ_ONLY_EXISTING_FINAL_ARTIFACTS', comparison=pin(args.comparison),
                  thresholdPercent=args.threshold, cohortSummary=comparison['summary'],
                  measurement='MX function bytes count final encoded instructions; Keil function segments may include tables. '
                              'Outside-function remainder includes tables, constants, runtime, startup and vector bytes. '
                              'Static byte differences are not promised removable bytes or runtime measurements.',
                  summary=summarize(rows), rows=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')
    for r in rows:
        print(f"{r['id']} {100*r['ratio']:.3f}% to120={r['bytesTo120']} to110={r['bytesTo110']} "
              f"functions={r['mxApplicationInstructionBytes']}/{r['keilFunctionSegmentBytes']} "
              f"outside={r['mxOutsideFunctionInstructions']}/{r['keilOutsideFunctionSegments']}")
        for p in r['pairs'][:8]:
            f = next(f for f in r['functions'] if f['cName'] == p['name'])
            print(' ', p, f['unit'], f['backend'], f['wideRefusal'])
        print(' unpaired:', r['mxUnpairedFunctions'], r['keilUnpairedFunctions'])
        print(' runtime:', r['runtimeReports'])


if __name__ == '__main__':
    main()
