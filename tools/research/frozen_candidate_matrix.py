"""Complete a hash-pinned candidate matrix without replacing earlier evidence."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import shutil
import time

from unified_sdk_matrix import one, pin, read, save


def verify(record):
    assert pin(record['path'])['sha256'] == record['sha256'], record['path']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    audit = read(args.audit)
    config = read(args.config)
    candidate = Path(audit['candidate']['root'])
    assert Path(config['core']).resolve() == (candidate/'core').resolve()
    assert Path(config['mxEntry']).resolve() == (candidate/'Build.ps1').resolve()
    for item in audit['candidate']['contents']:
        verify(dict(path=candidate/item['path'], sha256=item['sha256']))
    verify(audit['inventory']['frozenInputs'])
    inputs = {p['id']: p for p in read(audit['inventory']['frozenInputs']['path'])['projects']}
    historical = {p['id']: p for p in audit['historical']['projects']}
    reused = set(audit['currentMatrix']['completed'])
    assert set(inputs) == set(historical)
    config['output'] = str(args.output.resolve())
    args.output.mkdir(parents=True, exist_ok=False)
    save(args.output/'configuration.json', config)
    results = {}
    for pid in sorted(reused):
        old = historical[pid]
        for key in ('result', 'receipt', 'hex'):
            verify(old[key])
        result = read(old['result']['path'])
        assert result['status'] == 'BUILT'
        results[pid] = dict(id=pid, reused=True, result=old['result'], **{
            k: result[k] for k in ('status', 'payloadBytes', 'receipt', 'hex', 'wallSeconds')})
    pending = []
    for pid, project in inputs.items():
        if pid in reused:
            continue
        out = args.output/'projects'/pid/'mx251'
        out.mkdir(parents=True)
        origin = Path(project['root'])
        for item in project['files']:
            source = origin/item['path']
            verify(dict(path=source, sha256=item['sha256']))
            dest = out/item['path']
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
        pending.append(dict(id=pid, sourceManifest=project['inputManifest']))
    # Start expensive projects early so that their final link passes overlap
    # shorter builds. Every project still uses the same frozen public entry.
    pending.sort(key=lambda p: -read(historical[p['id']]['result']['path']).get('wallSeconds', 0))
    save(args.output/'run.json', dict(audit=pin(args.audit), inputConfig=pin(args.config),
        candidate=pin(candidate/'core/core.json'), runner=pin(__file__),
        engine=pin(Path(__file__).with_name('unified_sdk_matrix.py')),
        jobs=args.jobs, reused=sorted(reused), scheduled=[p['id'] for p in pending],
        hostTools=[pin(config[k]) for k in ('pwsh', 'cmake', 'ninja')]))
    started = time.monotonic()

    def summarize():
        rows = []
        for pid, result in sorted(results.items()):
            old = historical[pid]
            row = dict(**result, name=old['name'], cohort=old['cohort'],
                previousBytes=old['bytes'], references=old['references'],
                rawComparisonOnly=old['rawComparisonOnly'])
            if result['status'] == 'BUILT':
                n = result['payloadBytes']
                k = old['keilBytes']
                row.update(bytes=n, delta=n-old['bytes'], keilBytes=k,
                    keilPercent=100*n/k, gap110=max(0, n-k*110//100),
                    gap120=max(0, n-k*120//100))
            rows.append(row)
        normal = [r for r in rows if r['status'] == 'BUILT' and not r['rawComparisonOnly']]
        summary = dict(completed=len(results), total=len(inputs), reused=len(reused),
            built=sum(r['status'] == 'BUILT' for r in rows),
            failed=[r['id'] for r in rows if r['status'] != 'BUILT'],
            normalComparable=len(normal), at110=sum(not r['gap110'] for r in normal),
            at120=sum(not r['gap120'] for r in normal),
            elapsedSeconds=time.monotonic()-started,
            deltaBytes=sum(r['delta'] for r in rows if r['status'] == 'BUILT'))
        save(args.output/'results.json', dict(summary=summary, rows=rows))
        return summary

    print('START', len(pending), 'builds;', len(reused), 'reused;', args.jobs, 'workers', flush=True)
    summarize()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(one, config, p, 'mx251'): p['id'] for p in pending}
        for future in as_completed(futures):
            pid = futures[future]
            result = future.result()
            results[pid] = dict(id=pid, reused=False,
                result=pin(args.output/'projects'/pid/'mx251/result.json'),
                **{k: result[k] for k in ('status', 'payloadBytes', 'receipt', 'hex', 'wallSeconds', 'error')
                   if k in result})
            summary = summarize()
            print('PROGRESS', summary['completed'], '/', summary['total'],
                  'failed', len(summary['failed']), flush=True)
    for item in audit['candidate']['contents']:
        verify(dict(path=candidate/item['path'], sha256=item['sha256']))
    save(args.output/'completion.json', dict(status='PASS' if not summary['failed'] else 'HAS_FAILURES',
        candidateUnchanged=True, summary=summary, results=pin(args.output/'results.json')))
    print('COMPLETE', summary, flush=True)


if __name__ == '__main__':
    main()
