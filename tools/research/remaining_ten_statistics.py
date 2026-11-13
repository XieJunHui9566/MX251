"""Summarize frozen cohort artifacts; never compile, assemble or access a device.

Pattern byte counts describe existing instructions, not validated savings.
Carry observations compare explicit CY reads with source shifts and Keil output;
they do not claim standard C specifies the hardware flags of a C expression.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from call_layout import ihex, read_unit, sha
from loop_tail_study import fields, text


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def pin(path):
    return dict(path=str(path.resolve()), sha256=sha(path))


def immediate_compares(ins, labels):
    sites = []
    for at, end in enumerate(ins):
        op, args = fields(end)
        if op != 'cmp' or not re.fullmatch(r'dr\d+,dr20', args):
            continue
        for count in (1, 2):
            seq = ins[max(0, at-count):at+1]
            if len(seq) != count+1 or any(i.owner != end.owner for i in seq):
                continue
            if any(seq[0].address < a <= end.address for a in labels.values()):
                continue
            high = low = None
            valid = True
            for i in seq[:-1]:
                op, args = fields(i)
                if (op, args) == ('sub', 'wr20,wr20'):
                    high = 0
                elif (op, args) == ('sub', 'dr20,dr20'):
                    high = low = 0
                elif op == 'mov':
                    m = re.fullmatch(r'(wr20|wr22|dr20),#(0x[0-9a-f]+|[0-9]+)', args)
                    if not m:
                        valid = False
                        break
                    value = int(m[2], 0)
                    if m[1] == 'wr20':
                        high = value
                    elif m[1] == 'wr22':
                        low = value
                    else:
                        high, low = value >> 16, value & 65535
                else:
                    valid = False
                    break
            if valid and high is not None and low is not None:
                size = sum(len(i.data) for i in seq)
                encodable_class = high in (0, 65535)
                sites.append(dict(function=end.owner, line=seq[0].line,
                    literal=f'0x{high:04x}{low:04x}', currentBytes=size,
                    extension16=encodable_class,
                    nominalReductionAtFourBytes=size-4 if encodable_class else None,
                    instructions=[text(i) for i in seq]))
                break
    return sites


def source_function(source, name):
    """Small source excerpt locator for this inventory, not a C parser."""
    m = re.search(r'\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{', source)
    if not m:
        return None
    depth = 1
    end = m.end()
    while end < len(source) and depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return dict(line=source.count('\n', 0, m.start())+1, text=source[m.start():end])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--comparison', type=Path, required=True)
    ap.add_argument('--remaining', type=Path, required=True)
    ap.add_argument('--loops', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    comparison, remaining, loops = map(read, (args.comparison, args.remaining, args.loops))
    for report in (remaining, loops):
        assert report['comparison']['sha256'] == sha(args.comparison)
    rows = comparison['rows']
    residual = sorted(remaining['rows'], key=lambda r: (-r['bytesTo110'], r['id']))
    residual_ids = {r['id'] for r in residual}
    comparisons, carry, inputs = [], [], []
    for row in rows:
        result_path = Path(row['evidence']['candidate']['path'])
        result = read(result_path)
        root = Path(result['hex']['path']).parent
        memory = ihex(root/'firmware.hex')
        assert len(memory) == row['mx251Bytes']
        inputs += [pin(result_path), pin(root/'firmware.hex'), pin(root/'project.snapshot.json')]
        snapshot = read(root/'project.snapshot.json')
        for entry in snapshot['units']:
            if Path(entry['file']).suffix.lower() != '.c':
                continue
            unit = read_unit(root, entry['name'], memory)
            inputs += unit['inputs']
            comparisons += [dict(id=row['id'], unit=entry['name'], **site)
                            for site in immediate_compares(unit['instructions'], unit['labels'])]
            owners = defaultdict(list)
            for ins in unit['instructions']:
                if ins.owner:
                    owners[ins.owner].append(ins)
            for name, ins in owners.items():
                if not any(fields(i) == ('mov', 'c,_CY') for i in ins):
                    continue
                cname = re.sub(r'^_+m251r4_', '', name).lstrip('_')
                source_path = root.parents[2]/entry['file']
                source = source_path.read_text(encoding='gb18030', errors='replace')
                body = source_function(source, cname)
                assert body is not None, (row['id'], name)
                source_shift = bool(re.search(r'\b\w+\s*<<=\s*1\s*;', body['text']))
                keil = []
                for path in Path(row['evidence']['keil']['path']).parent.glob('unit*.lst'):
                    listing = path.read_text(encoding='latin1')
                    m = re.search(r';\s+FUNCTION '+re.escape(cname)+r' \(BEGIN\)(.*?)'
                                  r';\s+FUNCTION '+re.escape(cname)+r' \(END\)', listing, re.S|re.I)
                    if m:
                        keil.append(dict(input=pin(path),
                            leftShifts=re.findall(r'^([0-9A-F]{6}\s+[0-9A-F]+\s+SLL\s+\S+)', m[1], re.M)))
                asm = (root/(entry['name']+'.asm')).read_text(encoding='latin1')
                m = re.search(r'; mx251-function begin[^\n]*symbol='+re.escape(name)+r'\s.*?'
                              r'; mx251-function end[^\n]*symbol='+re.escape(name), asm, re.S)
                carry.append(dict(id=row['id'], unit=entry['name'], function=name,
                    mxBytes=sum(len(i.data) for i in ins), source=pin(source_path),
                    sourceFunctionLine=body['line'], sourceHasShiftByOne=source_shift,
                    mxInputDplReads=[text(i) for i in ins if re.search(r',dpl$', fields(i)[1])],
                    mxLeftShifts=[text(i) for i in ins if fields(i)[0] in ('sll', 'rl')],
                    mxInstructions=[dict(line=i.line, instruction=text(i)) for i in ins],
                    backend='legacy' if 'selected=legacy' in m[0] else 'wide', keil=keil))
    extended = [s for s in comparisons if s['extension16']]
    distribution = {f'within{p}': sum(100*r['mx251Bytes'] <= p*r['keilBytes'] for r in rows)
                    for p in (100, 110, 120)}
    partitions = [dict(id=r['id'], name=r['name'], mx=r['mxBytes'], keil=r['keilBytes'],
        ratioPercent=100*r['ratio'], bytesTo110=r['bytesTo110'], partition=r['partition'],
        defaultRetiBytes=r['vectorDefaultRetiCount'], topFunctions=r['pairs'][:8]) for r in residual]
    repeat_families = defaultdict(list)
    for r in residual:
        for pair in r['pairs']:
            repeat_families[pair['name']].append(dict(id=r['id'], **pair))
    repeated = [dict(name=name, observations=items, netGap=sum(p['delta'] for p in items))
                for name, items in repeat_families.items() if len(items) >= 2]
    repeated.sort(key=lambda x: -x['netGap'])
    remaining_loops = [p for p in loops['projects'] if p['id'] in residual_ids and p['functions']]
    carry_summary = dict(projects=len({s['id'] for s in carry}), functions=len(carry),
        sourceShiftByOne=sum(s['sourceHasShiftByOne'] for s in carry),
        missingDplRead=sum(not s['mxInputDplReads'] for s in carry),
        missingSllOrRl=sum(not s['mxLeftShifts'] for s in carry),
        keilHasSll=sum(any(k['leftShifts'] for k in s['keil']) for s in carry),
        backends=dict(Counter(s['backend'] for s in carry)),
        ids=[s['id'] for s in carry], remainingIds=sorted({s['id'] for s in carry} & residual_ids))
    summary = dict(projects=len(rows), **distribution, mxBytes=comparison['summary']['mxTotalBytes'],
        keilBytes=comparison['summary']['keilTotalBytes'], remainingProjects=len(residual),
        bytesTo110=sum(r['bytesTo110'] for r in residual),
        topFourBytesTo110=sum(r['bytesTo110'] for r in residual[:4]),
        remainingDefaultRetiBytes=sum(r['vectorDefaultRetiCount'] for r in residual),
        remainingStartupVectorGap=sum(r['partition']['mx']['startupVectors']-
            r['partition']['keil']['startupVectors'] for r in residual),
        loopTailInventory=loops['summary'], carryCompatibility=carry_summary,
        compare32=dict(sites=len(comparisons), extension16Sites=len(extended),
            projects=len({s['id'] for s in extended}),
            nominalReductionAtFourBytes=sum(s['nominalReductionAtFourBytes'] for s in extended),
            remainingNominalReduction=sum(s['nominalReductionAtFourBytes'] for s in extended if s['id'] in residual_ids)))
    report = dict(status='READ_ONLY_STATISTICS_AND_COMPATIBILITY_FINDINGS', date='2026-11-13',
        script=pin(Path(__file__)), comparison=pin(args.comparison),
        remainingInput=pin(args.remaining), loopInput=pin(args.loops), summary=summary,
        remaining=partitions, repeatedFunctionNames=repeated, remainingLoopTail=remaining_loops,
        compare32=comparisons, carryCompatibility=carry, inputs=inputs,
        scope='No compiler, assembler, linker, device or SDK mutation. Size thresholds are size only. '
              'Equal function names identify comparisons, not an optimizer rule or proof of identical source. '
              'Loop/tail and immediate counts are search leads, not measured savings. '
              'Default RETI bytes cannot be removed without changing unexpected-interrupt behavior. '
              'CY findings concern Keil-compatible flag-dependent C, not ISO C shift semantics. '
              'Carry search matches the explicit _CY spelling in these frozen inputs; it is not a complete flag audit.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8', newline='\n')
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
