"""Join unchanged-input AI8051U/STC32G rebuilds and retained reference images.

Reference compiler results are retained runs, not newly compiled results.
Keep each STC math mode and each historical MX cohort separate.
"""
import argparse
from collections import Counter
from pathlib import Path

from unified_sdk_matrix import image, pin, read, save


def reference(path, expected=None, code_min=0xff0000):
    value = read(path)
    result = dict(status=value['status'], bytes=value.get('payloadBytes'),
                  model=value.get('model'), result=pin(path))
    if expected is not None:
        assert result['bytes'] == expected, (path, result, expected)
    if value['status'] == 'BUILT':
        hexfile = path.parent/'firmware.hex'
        if not hexfile.exists():
            hexfile = path.parent/Path(value['hex']['path']).name
        stats, _ = image(hexfile, code_min)
        assert stats['payloadBytes'] == value['payloadBytes'], path
        assert stats['hex']['sha256'] == value['hex']['sha256'], path
        result['hex'] = stats['hex']
    return result


def rebuilt(directory, compiler, inputs):
    for item in inputs:
        assert pin(directory/item['relativePath'])['sha256'] == item['sha256'], (directory, item)
    path = directory/'result.json'
    value = read(path)
    out = dict(status=value['status'], bytes=value.get('payloadBytes'),
               result=pin(path), sourceInputsIdentical=True,
               buildWallSeconds=value.get('wallSeconds'),
               configuration=read(directory/'mx251.json'))
    if value['status'] == 'BUILT':
        receipt = directory/'.build/mx251/latest/receipt.json'
        record = read(receipt)
        assert record['status'] == 'complete' and record['compilerSha256'] == compiler, directory
        stats, _ = image(receipt.parent/'firmware.hex', 0xfe0000)
        assert stats['payloadBytes'] == value['payloadBytes'], path
        assert stats['hex']['sha256'] == record['firmwareSha256'], path
        out.update(receipt=pin(receipt), hex=stats['hex'])
    else:
        out['error'] = value.get('error')
    return out


def summary(rows, reference_name):
    paired = [r for r in rows if r['status'] == 'BUILT' and r['references'][reference_name]['status'] == 'BUILT']
    result = dict(projects=len(rows), built=sum(r['status'] == 'BUILT' for r in rows),
                  compared=len(paired), reference=reference_name)
    for threshold in (110, 120):
        result['at'+str(threshold)] = sum(100*r['bytes'] <= threshold*r['references'][reference_name]['bytes'] for r in paired)
        result['over'+str(threshold)] = [r['id'] for r in paired if 100*r['bytes'] > threshold*r['references'][reference_name]['bytes']]
        result['new'+str(threshold)] = [r['id'] for r in paired if 100*r['bytes'] <= threshold*r['references'][reference_name]['bytes'] < 100*r['before']]
        result['lost'+str(threshold)] = [r['id'] for r in paired if 100*r['before'] <= threshold*r['references'][reference_name]['bytes'] < 100*r['bytes']]
    built = [r for r in rows if r['status'] == 'BUILT']
    result.update(beforeBytes=sum(r['before'] for r in built), afterBytes=sum(r['bytes'] for r in built),
                  smaller=sum(r['bytes'] < r['before'] for r in built),
                  larger=sum(r['bytes'] > r['before'] for r in built),
                  unchanged=sum(r['bytes'] == r['before'] for r in built),
                  referenceStatuses=dict(Counter(r['references'][reference_name]['status'] for r in rows)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    args = parser.parse_args()
    root, base = args.root, args.baseline
    core = read(root/'stc32g/candidate/core/core.json')
    compiler = core['compiler']['sha256']
    assert pin(root/'stc32g/candidate/core'/core['compiler']['path'])['sha256'] == compiler
    rows, manifests = [], []
    expanded_refs = {r['id']: r['references'] for r in read(root/'ai8051u/references-expanded31.json')['rows']}
    for filename, cohort in [('inputs-frozen75.json', 'ai8051u-frozen75'), ('inputs-expanded31.json', 'ai8051u-expanded31')]:
        manifest = root/'ai8051u'/filename
        manifests.append(pin(manifest))
        for project in read(manifest)['projects']:
            key = project['id']
            directory = Path(project.get('finalBuildDirectory', root/'ai8051u/projects'/key/'mx251'))
            row = dict(id=key, name=project['name'], cohort=cohort,
                       **rebuilt(directory, compiler, project['inputs']))
            if cohort == 'ai8051u-frozen75':
                previous = project['previous']
                row['before'] = previous['mx251Bytes']
                row['references'] = {suite: reference(Path(previous['evidence'][suite]['path']), previous[suite+'Bytes']) for suite in ('keil','stc')}
                row['semanticNotes'] = previous.get('semanticNotes')
                row['declaredInputOverride'] = previous.get('declaredInputOverride')
            else:
                row['before'] = project['previousBuild']['payloadBytes']
                row['references'] = {suite: reference(Path(ref['result']['path']), ref['bytes']) for suite,ref in expanded_refs[key].items()}
                row['historicalInputOrigin'] = project['inputOrigin']
            rows.append(row)
    parent_file = Path(read(root/'stc32g/comparison.json')['parentReport']['path'])
    parent = parent_file.parent
    manifests.append(pin(parent_file))
    for previous in read(parent_file)['rows']:
        if previous['status'] != 'BUILT':
            continue
        key = previous['id']
        origin = parent/'projects'/key/'mx251'
        inputs = [dict(relativePath=p.relative_to(origin).as_posix(), sha256=pin(p)['sha256'])
                  for p in list((origin/'src').rglob('*')) + [origin/n for n in ('CMakeLists.txt','mx251.json','dialect.json')] if p.is_file()]
        refs = base/'work/stc32g-box-20261114/projects'/key
        rows.append(dict(id=key, name=previous['name'], cohort='stc32g-97', before=previous['new'],
                         **rebuilt(root/'stc32g/projects'/key/'mx251', compiler, inputs),
                         references={suite: reference(refs/folder/'result.json',previous[column],0xfe0000)
                                     for suite,folder,column in [('keil','keil','keil'),('stc-soft','stc-soft','stc')]}))
    summaries={}
    for cohort,suites in [('ai8051u-frozen75',['keil','stc']),('ai8051u-expanded31',['keil','stc-soft','stc-mdu32']),('stc32g-97',['keil','stc-soft'])]:
        selected=[r for r in rows if r['cohort']==cohort]
        summaries[cohort]={suite:summary(selected,suite) for suite in suites}
    summaries['ai8051u-106']={'keil':summary([r for r in rows if r['id'].startswith('p')],'keil')}
    assert len(rows) == 203 and len({r['id'] for r in rows}) == 203
    for row in rows:
        row['rawComparisonOnly'] = row['id'] in ('g083','p144','p145')
    qualified_scope = {
        corpus: summary([r for r in rows if r['id'].startswith(prefix) and not r['rawComparisonOnly']], 'keil')
        for corpus,prefix in [('ai8051u','p'),('stc32g','g')]}
    ai_inventory=read(base/'reference/sdk-expanded/inventory.json')
    ai_coverage={r['id']:r for r in read(base/'reference/sdk-expanded/coverage-summary.json')['rows']}
    rebuilt_ids={r['id'] for r in rows}
    excluded_ai=[dict(id=r['id'],name=r['name'],status=ai_coverage[r['id']]['mx']['status'],libraries=ai_coverage[r['id']]['libraries'])
                 for r in ai_inventory['rows'] if r['cSources'] and r['id'] not in rebuilt_ids]
    stc_inventory=read(base/'work/stc32g-box-20261114/comparison.json')
    excluded_stc=[dict(id=r['id'],name=r['name'],status=r['results']['mx251']['status'],libraries=r['libraries'])
                  for r in stc_inventory['rows'] if r['cSources'] and r['id'] not in rebuilt_ids]
    output=dict(schema=1,status='PASS_FULL_MATRIX_BUILDS' if all(r['status']=='BUILT' for r in rows) else 'BUILD_FAILURES',
                compilerSha256=compiler,compilerSourceCommit=core['compilerSourceCommit'],core=pin(root/'stc32g/candidate/core/core.json'),
                comparisonKind='Current unified MX core versus retained Keil/STC reference runs',
                measurement='HEX payload bytes; holes excluded; integer threshold comparison',
                runtimeMeasured=False,hardwareExecuted=False,sourceAlgorithmsChanged=False,
                functionGcPolicy=dict(script=pin(root/'stc32g/candidate/core/build/qualified/FunctionGc.ps1'),
                                      sourceCommit=core.get('gcZeroAddendSourceCommit'),
                                      replay=pin(root/'gc-replay.json') if (root/'gc-replay.json').exists() else None,
                                      method='Replay original assembly for all 203 projects; retain existing builds where candidate assembly is identical, rebuild changed projects with the same compiler.'),
                inputManifests=manifests,rows=rows,summary=summaries,withoutKnownComparisonLimitations=qualified_scope,
                excluded=dict(ai8051u=excluded_ai,stc32g=excluded_stc),
                assemblyNotRebuilt=dict(ai8051u=ai_inventory['assemblyProjects'],stc32g=stc_inventory['assemblyProjects']),
                limitations=['AI expanded31 historical MX builds are from several revisions. Five math projects retain the native-math configurations, including p076/p077/p079; this is not the historical per-project minimum across runtimes.',
                             'AI frozen75 STC references use MDU32; expanded31 has separate software and MDU32 rows; STC32G uses software math.',
                             'p144/p145 retain large model; raw ratios against Keil XSMALL are not same-model qualification.',
                             'g083 retains an original printf argument-width mismatch and full-format-runtime fallback; its raw size is not a valid normal-program optimization comparison.',
                             'Prior source errata, dialect adapters and input overrides are retained; no application changes made in this run.',
                             'Unbuilt OMF and pure A251 inputs are inventoried, not silently included in size-pass counts.'])
    save(root/'comparison.json',output)
    for cohort,refs in summaries.items():
        for name,stats in refs.items():
            print(cohort,name,stats)


if __name__=='__main__':
    main()
