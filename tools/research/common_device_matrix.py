"""Stage architecture/device policy changes over an unchanged compiler corpus."""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import shutil
import subprocess

import stc32g_box_matrix as box
from unified_sdk_matrix import read, save, pin

REPO = Path(__file__).resolve().parents[2]


def prepare(root, parent):
    root.mkdir(parents=True, exist_ok=True)
    core = root/'candidate/core'
    if not core.exists():
        shutil.copytree(parent/'candidate', root/'candidate')
        for name in ('TargetContracts.json', 'TargetProfile.ps1', 'README.txt'):
            shutil.copy2(REPO/'core/build/qualified'/name, core/'build/qualified'/name)
        registry = pin(core/'build/qualified/TargetContracts.json')['sha256']
        for path in (core/'profiles').glob('*/profile.lock.json'):
            value = read(path)
            value['targetContract']['registrySha256'] = registry
            save(path, value)
        value = read(core/'core.json')
        value.update(buildId='mx251-common-device-semantics-20261114',
                     sourceCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                     parentCore=pin(parent/'candidate/core/core.json'))
        save(core/'core.json', value)
        shutil.copy2(parent/'Build-Optimized.ps1', root/'Build-Optimized.ps1')
    cfg = dict(read(parent/'config.json'), output=str(root.resolve()), core=str(core.resolve()),
               mxEntry=str((root/'candidate/Build.ps1').resolve()))
    save(root/'config.json', cfg)
    inputs = read(parent/'inputs.json')
    save(root/'inputs.json', inputs)
    built = {r['id'] for r in read(parent/'comparison.json')['rows'] if r['status'] == 'BUILT'}
    projects = [p for p in inputs['projects'] if p['id'] in built]
    for p in projects:
        src = parent/'projects'/p['id']/'mx251'
        dst = root/'projects'/p['id']/'mx251'
        if not dst.exists():
            shutil.copytree(src/'src', dst/'src')
            for name in ('mx251.json', 'CMakeLists.txt', 'dialect.json'):
                shutil.copy2(src/name, dst/name)
    return cfg, projects


def refusals(latest):
    result = set()
    for path in latest.glob('u*.asm'):
        for fn, reason in re.findall(r'; wide refusal function=(\S+) stage=\S+ reason=(\S+)', path.read_text(errors='replace')):
            result.add((path.stem, fn, reason))
    return result


def report(root, parent):
    previous = read(parent/'comparison.json')
    rows = []
    compiler = read(root/'candidate/core/core.json')['compiler']['sha256']
    counts = Counter()
    for old in previous['rows']:
        if old['status'] != 'BUILT':
            continue
        project = root/'projects'/old['id']/'mx251'
        if not (project/'result.json').exists():
            continue
        result = read(project/'result.json')
        assert result['status'] == 'BUILT', (old['id'], result)
        latest = project/'.build/mx251/latest'
        receipt = read(latest/'receipt.json')
        assert receipt['compilerSha256'] == compiler
        prior = parent/'projects'/old['id']/'mx251'
        for p in list((prior/'src').rglob('*')) + [prior/n for n in ('mx251.json','CMakeLists.txt','dialect.json')]:
            if p.is_file():
                assert p.read_bytes() == (project/p.relative_to(prior)).read_bytes(), p
        reasons = refusals(latest)
        counts.update(r[2] for r in reasons)
        before_reasons = refusals(prior/'.build/mx251/latest')
        rows.append(dict(id=old['id'], name=old['name'], before=old['new'], new=result['payloadBytes'],
                         keil=old['keil'], stc=old['stc'], result=pin(project/'result.json'),
                         refusalsBefore=sorted(before_reasons), refusalsAfter=sorted(reasons)))
    summary = dict(rebuilt=len(rows), beforeBytes=sum(r['before'] for r in rows), afterBytes=sum(r['new'] for r in rows),
                   smaller=sum(r['new']<r['before'] for r in rows), larger=sum(r['new']>r['before'] for r in rows),
                   unchanged=sum(r['new']==r['before'] for r in rows), refusalReasons=dict(counts))
    for reference in ('keil','stc'):
        selected = [r for r in rows if r[reference]]
        summary[reference] = dict(count=len(selected), at110=sum(r['new']*100 <= r[reference]*110 for r in selected),
                                  at120=sum(r['new']*100 <= r[reference]*120 for r in selected))
    save(root/'comparison.json', dict(schema=1, compilerSha256=compiler, parentReport=pin(parent/'comparison.json'),
                                     sourceAlgorithmsChanged=False, hardwareExecuted=False, rows=rows, summary=summary))
    print(summary, flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('--parent', required=True, type=Path)
    p.add_argument('--ids', default='all')
    p.add_argument('--jobs', default=4, type=int)
    p.add_argument('--report-only', action='store_true')
    a = p.parse_args()
    if not a.report_only:
        cfg, projects = prepare(a.root, a.parent)
        projects = [p for p in projects if a.ids=='all' or p['id'] in a.ids.split(',')]
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            list(pool.map(lambda project: box.one(cfg, project, 'mx251'), projects))
    report(a.root, a.parent)
