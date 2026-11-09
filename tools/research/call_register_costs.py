"""Derive one callee-placement trial from an existing complete build.

This is a research preference, not a preservation contract. The normal build
must recompute actual call clobbers after allocation. SDK inputs are read only.
"""
import argparse
import json
from pathlib import Path
import re

from call_layout import audit, sha


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


FUNCTION = re.compile(r'^; mx251-function begin schema=1 symbol=(\S+).*?\n'
                      r'(.*?)^; mx251-function end schema=1 symbol=\1\s*$', re.M | re.S)
CALL = re.compile(r'; mx251-call-effects schema=1 caller=(\S+) site=(\d+) '
                  r'callee=(\S+) abi=(\d+) live=([0-9a-f]+) setup=([0-9a-f]+) '
                  r'finish=([0-9a-f]+) complete=1 saved=([^\r\n]*)')


def derive(build):
    measured = audit(build)
    config = read(build / 'project.snapshot.json')
    feedback = read(build / 'call-clobber-feedback.json')
    functions, demands = {}, {}
    for unit in config['units']:
        text = (build / (unit['name'] + '.asm')).read_text(encoding='latin1')
        for name, body in FUNCTION.findall(text):
            functions.setdefault(name, []).append(dict(unit=unit['name'], body=body))
        for caller, site, callee, abi, live, setup, finish, saved in CALL.findall(text):
            row = demands.setdefault(callee, dict(costs=[0] * 32, sites=[], groups=[]))
            mask = int(live, 16) & ~(int(setup, 16) | int(finish, 16))
            for i in range(32):
                row['costs'][i] += bool(mask & (1 << i))
            row['sites'].append(dict(unit=unit['name'], caller=caller, site=int(site), live=mask))
            row['groups'] += [int(g, 16) for g in saved.split(',') if g and not
                              (int(g, 16) & (int(setup, 16) | int(finish, 16)))]
    candidates = []
    for binding in feedback['bindings']:
        name = binding['symbol']
        if name not in demands or len(functions.get(name, [])) != 1:
            continue
        body = functions[name][0]['body']
        if 'wide allocation selected=' not in body or 'selected=legacy ' in body:
            continue
        row = demands[name]
        # Count only sites currently paying for the callee's known writes.
        conflicts = sum(bool(group & binding['writes']) for group in row['groups'])
        if not conflicts:
            continue
        instructions = len(re.findall(r'^\s+\w+\s', body, re.M))
        candidates.append(dict(symbol=name, unit=binding['unit'], costs=row['costs'],
                               sites=row['sites'], conflictingSaveGroups=conflicts,
                               instructions=instructions,
                               priority=conflicts / max(1, instructions),
                               baselineWrites=binding['writes']))
    candidates.sort(key=lambda r: (-r['priority'], r['symbol']))
    return dict(schema=1, baseline=str(build), baselineCodeBytes=measured['codeBytes'],
                receiptSha256=sha(build / 'receipt.json'),
                demandCoverage='selected wide-backend call sites; static counts, not execution frequency',
                candidates=candidates)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = derive(args.baseline.resolve())
    args.output.mkdir(parents=True, exist_ok=False)
    if result['candidates']:
        chosen = result['candidates'][0]
        contract = args.output / 'register-costs.txt'
        contract.write_text('MX251-REGISTER-COSTS-1\n' + chosen['symbol'] + ' ' +
                            ' '.join(map(str, chosen['costs'])) + '\n', encoding='ascii')
        result.update(chosen=chosen, contractSha256=sha(contract))
    (args.output / 'selection.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps([dict(symbol=r['symbol'], groups=r['conflictingSaveGroups'],
                           instructions=r['instructions']) for r in result['candidates'][:8]], indent=2))
