"""Inventory address reconstruction and parameterized tails in frozen images.

Reads final ASM/RST/HEX only. Candidate counts are search leads, not proven
transformations or savings. No compiler, assembler, linker or device is run.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from call_layout import ihex, read_unit, sha

BRANCH = {'ejmp', 'sjmp', 'ljmp', 'ajmp', 'jc', 'jnc', 'jz', 'jnz', 'jb', 'jnb',
          'jbc', 'cjne', 'djnz', 'je', 'jne', 'jg', 'jle', 'jsg', 'jsge', 'jsl', 'jsle'}
GOTO = {'ejmp', 'sjmp', 'ljmp', 'ajmp'}
RETURN = {'ret', 'eret', 'reti'}


def text(ins):
    return re.sub(r'\s+', ' ', ins.source.split(';', 1)[0].strip())


def fields(ins):
    s = text(ins).split(' ', 1)
    return s[0], s[1].replace(' ', '') if len(s) == 2 else ''


def natural_loops(ins, labels):
    """Instruction CFG, dominator-qualified back edges and reverse closure."""
    by_address = {i.address: n for n, i in enumerate(ins)}
    successors, predecessors = [], [set() for _ in ins]
    for n, i in enumerate(ins):
        op, args = fields(i)
        succ = set()
        if op not in GOTO | RETURN and n + 1 < len(ins):
            succ.add(n + 1)
        if op in BRANCH:
            target = by_address.get(labels.get(args.rsplit(',', 1)[-1]))
            if target is not None:
                succ.add(target)
        successors.append(succ)
        for target in succ:
            predecessors[target].add(n)
    reachable, pending = set(), [0]
    while pending:
        node = pending.pop()
        if node in reachable:
            continue
        reachable.add(node)
        pending.extend(successors[node] - reachable)
    all_bits = sum(1 << n for n in reachable)
    dom = [all_bits if n in reachable else 0 for n in range(len(ins))]
    dom[0] = 1
    changed = True
    while changed:
        changed = False
        for n in sorted(reachable - {0}):
            common = all_bits
            for pred in predecessors[n] & reachable:
                common &= dom[pred]
            value = common | (1 << n)
            if dom[n] != value:
                dom[n] = value
                changed = True
    loops = {}
    for latch in sorted(reachable):
        for header in successors[latch]:
            if not dom[latch] & (1 << header):
                continue
            members, todo = {header, latch}, [] if header == latch else [latch]
            while todo:
                node = todo.pop()
                for pred in predecessors[node] & reachable:
                    if pred not in members:
                        members.add(pred)
                        todo.append(pred)
            loops.setdefault(header, set()).update(members)
    return loops


def address_sites(ins, labels):
    computed = any(fields(i)[0] == 'jmp' or
                   fields(i)[0] in GOTO and fields(i)[1].startswith('@') for i in ins)
    loops = {} if computed else natural_loops(ins, labels)
    sites = []
    for at in range(len(ins) - 4):
        seq = ins[at:at + 5]
        ops = [fields(i) for i in seq]
        copy = re.fullmatch(r'dr(\d+),dr(\d+)', ops[0][1])
        index = re.fullmatch(r'wr22,(r\d+|wr\d+)', ops[1][1])
        if not (ops[0][0] == 'mov' and copy and ops[1][0] in ('mov', 'movz') and index
                and ops[2] == ('sub', 'wr20,wr20')
                and ops[3] == ('add', 'dr'+copy[1]+',dr20')
                and ops[4] == ('anl', 'wr'+copy[1]+',#0x00ff')):
            continue
        # A secondary label inside the five instructions breaks a single site.
        if any(seq[0].address < value <= seq[-1].address for value in labels.values()):
            continue
        enclosing = [h for h, members in loops.items() if set(range(at, at + 5)) <= members]
        inner = min(enclosing, key=lambda h: len(loops[h])) if enclosing else None
        members = loops[inner] if inner is not None else set()
        sites.append(dict(line=seq[0].line, address=seq[0].address,
            bytes=sum(len(i.data) for i in seq), instructions=[text(i) for i in seq],
            index=index[1], base='dr'+copy[2], result='dr'+copy[1],
            inNaturalLoop=inner is not None,
            loopHeader=ins[inner].address if inner is not None else None,
            loopHasCall=any(fields(ins[n])[0] in ('ecall','lcall','acall') for n in members),
            nextInstructions=[text(i) for i in ins[at+5:at+9]]))
    return sites


def tail_candidates(ins, labels):
    """One largest disjoint suffix cluster per function; immediate holes only.

    Instructions and registers otherwise match exactly. We report repeated
    identical bytes, excluding differing literal instructions and redirects.
    Their removal is not guaranteed: value live ranges/flags still need proof.
    """
    starts = set(labels.values())
    tails = []
    for end, i in enumerate(ins):
        op, _ = fields(i)
        if op not in RETURN | GOTO:
            continue
        seq = [end]
        for n in range(end - 1, max(-1, end - 40), -1):
            previous_op, _ = fields(ins[n])
            if ins[n+1].address in starts or previous_op in BRANCH | RETURN | {'ecall','lcall','acall'}:
                break
            seq.append(n)
        tails.append(seq)
    groups = defaultdict(list)
    for seq in tails:
        keys = []
        for count, n in enumerate(seq, 1):
            op, args = fields(ins[n])
            # Only literal MOV operands are parameter holes. Symbolic addresses,
            # masks, shifts and arithmetic constants remain exact in this pass.
            key = op+' '+(re.sub(r'#0x[0-9a-f]+', '#IMM', args) if op == 'mov' else args)
            keys.append(key)
            if count >= 3:
                groups[tuple(keys)].append(seq[:count])
    candidates = []
    for keys, copies in groups.items():
        if len(copies) < 2:
            continue
        flat = [n for seq in copies for n in seq]
        if len(set(flat)) != len(flat):
            continue
        varied = [n for n in range(len(keys)) if len({text(ins[s[n]]) for s in copies}) > 1]
        if not varied:
            continue
        # Count only exact instruction text shared by all copies; no aggregate
        # claim about instructions with different operands or newly added moves.
        repeated = sum(sum(len(ins[s[n]].data) for s in copies[1:])
                       for n in range(len(keys)) if n not in varied)
        if repeated <= 4*(len(copies)-1):
            continue
        candidates.append(dict(copies=len(copies), instructionsPerCopy=len(keys),
            differingMovPositions=len(varied), repeatedExactBytes=repeated,
            redirectBudget=4*(len(copies)-1),
            bodyBytes=[sum(len(ins[n].data) for n in seq) for seq in copies],
            starts=[ins[seq[-1]].line for seq in copies],
            bodies=[[text(ins[n]) for n in reversed(seq)] for seq in copies]))
    return max(candidates, key=lambda c: c['repeatedExactBytes']-c['redirectBudget']) if candidates else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    comparison = json.loads(args.comparison.read_text(encoding='utf8'))
    projects = []
    for row in comparison['rows']:
        result = json.loads(Path(row['evidence']['candidate']['path']).read_text(encoding='utf8'))
        root = Path(result['hex']['path']).parent
        assert result['status'] == 'BUILT' and sha(root/'firmware.hex') == result['hex']['sha256']
        memory = ihex(root/'firmware.hex')
        assert len(memory) == row['mx251Bytes']
        functions, inputs = [], []
        for path in sorted(root.glob('u*.asm')):
            if not re.fullmatch(r'u\d+\.asm', path.name):
                continue
            unit = read_unit(root, path.stem, memory)
            inputs += unit['inputs']
            owners = defaultdict(list)
            for ins in unit['instructions']:
                if ins.owner:
                    owners[ins.owner].append(ins)
            for name, ins in owners.items():
                sites = address_sites(ins, unit['labels'])
                tail = tail_candidates(ins, unit['labels'])
                if sites or tail:
                    functions.append(dict(unit=path.stem, name=name, bytes=sum(len(i.data) for i in ins),
                        flags=unit['flags'][name], addressSites=sites, parameterizedTail=tail))
        projects.append(dict(id=row['id'],name=row['name'],functions=functions,inputs=inputs,
            receipt=result['receipt'],hex=result['hex']))
    sites=[(p['id'],f,s) for p in projects for f in p['functions'] for s in f['addressSites']]
    loop_sites=[r for r in sites if r[2]['inNaturalLoop']]
    tails=[(p['id'],f) for p in projects for f in p['functions'] if f['parameterizedTail']]
    summary=dict(projects=len(projects),addressSites=len(sites),loopAddressSites=len(loop_sites),
        loopAddressProjects=len({p for p,f,s in loop_sites}),
        loopAddressFunctions=len({(p,f['unit'],f['name']) for p,f,s in loop_sites}),
        loopAddressCurrentBytes=sum(s['bytes'] for p,f,s in loop_sites),
        callFreeLoopAddressSites=sum(not s['loopHasCall'] for p,f,s in loop_sites),
        parameterizedTailProjects=len({p for p,f in tails}),parameterizedTailFunctions=len(tails),
        repeatedExactTailBytes=sum(f['parameterizedTail']['repeatedExactBytes'] for p,f in tails))
    record=dict(status='READ_ONLY_FEASIBILITY',comparison=dict(path=str(args.comparison),sha256=sha(args.comparison)),
        script=dict(path=str(Path(__file__).resolve()),sha256=sha(Path(__file__))),summary=summary,projects=projects,
        scope='Address sites: exact unsigned extension/copy/add/mask sequence inside dominator-qualified loops. '
              'Tails: largest disjoint suffix cluster per function with literal MOV holes only. '
              'Counts are static inventory, not transform eligibility, savings or runtime measurements.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(summary,ensure_ascii=False))
    for p,f in sorted(tails,key=lambda r:-r[1]['parameterizedTail']['repeatedExactBytes'])[:12]:
        c=f['parameterizedTail'];print('tail',p,f['name'],c['copies'],c['repeatedExactBytes'],c['differingMovPositions'])
    totals=Counter()
    for p,f,s in loop_sites:totals[(p,f['name'])]+=s['bytes']
    print('loop address current bytes:',totals.most_common(15))


if __name__ == '__main__':
    main()
