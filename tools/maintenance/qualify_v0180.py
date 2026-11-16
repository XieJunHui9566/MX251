"""Summarize the frozen release builds and existing board/IDE evidence."""
import argparse
from datetime import datetime
from pathlib import Path
import shutil

from release_v0180 import REPO, read, save, sha


def pin(path):
    path = Path(path)
    return dict(path=str(path), sha256=sha(path))


def verify(record):
    path = Path(record['path'])
    assert sha(path) == record['sha256'], path
    return path


def main(work, distribution, repo_output=True):
    matrix = read(work/'matrix-final/results.json')
    completion = read(work/'matrix-final/completion.json')
    assert completion['candidateUnchanged']
    assert matrix['summary']['completed'] == matrix['summary']['total'] == 203
    audit = read(work/'final-audit.json')
    compiler = read(distribution/'core/core.json')['compiler']['sha256']
    rows = {r['id']: r for r in matrix['rows']}
    supplements = read(work/'supplemental-builds.json')
    for pid, item in supplements.items():
        result = read(Path(item['result']))
        row = rows[pid]
        row['supersededAttempt'] = row['result']
        row['supplementReason'] = item['reason']
        row['result'] = pin(item['result'])
        for key in ('status', 'payloadBytes', 'receipt', 'hex', 'wallSeconds'):
            row[key] = result[key]
        row.pop('error', None)
    for row in rows.values():
        assert row['status'] == 'BUILT', row['id']
        verify(row['result'])
        receipt = read(verify(row['receipt']))
        verify(row['hex'])
        assert receipt['compilerSha256'] == compiler, row['id']
        assert receipt['firmwareSha256'] == row['hex']['sha256'], row['id']
        n = receipt['codeBytes']
        assert n == row['payloadBytes']
        k = row['references']['keil']['bytes']
        row.update(bytes=n, delta=n-row['previousBytes'], keilBytes=k,
                   keilPercent=100*n/k, gap110=max(0, n-k*110//100),
                   gap120=max(0, n-k*120//100))
    rows = sorted(rows.values(), key=lambda r: r['id'])
    normal = [r for r in rows if not r['rawComparisonOnly']]

    def compare(items, reference):
        items = [r for r in items if r['references'][reference]['status'] == 'BUILT'
                 and r['references'][reference].get('bytes', 0) > 0]
        return dict(comparable=len(items),
            at110=sum(r['bytes']*100 <= r['references'][reference]['bytes']*110 for r in items),
            at120=sum(r['bytes']*100 <= r['references'][reference]['bytes']*120 for r in items))

    comparisons = {ref: compare(normal, ref) for ref in ('keil', 'stc-soft')}
    cohorts = {name: dict(projects=sum(r['cohort'] == name for r in rows),
        **{ref: compare([r for r in normal if r['cohort'] == name], ref)
           for ref in ('keil', 'stc-soft')}) for name in sorted({r['cohort'] for r in rows})}
    comparison = dict(version='0.180', compilerSha256=compiler, projects=203,
        normalComparable=len(normal), referencesReused=True,
        measurement='Intel HEX payload bytes, including linked runtime and startup',
        comparisons=comparisons, cohorts=cohorts,
        previousCompilerComparison=dict(scope='shared-cost-batch historical frozen matrix',
            smaller=sum(r['delta'] < 0 for r in normal),
            unchanged=sum(r['delta'] == 0 for r in normal),
            larger=sum(r['delta'] > 0 for r in normal),
            netByteChange=sum(r['delta'] for r in normal)),
        inventory=audit['inventory'], rows=rows)

    portable = read(work/'portable-smoke/results.json')
    assert len(portable) == 3 and all(r['exitCode'] == 0 and r['compilerSha256'] == compiler for r in portable)
    stress = read(work/'user-stress/compilation.json')
    board = read(work/'hardware-user-stress/result.json')
    assert stress['status'] == board['status'] == 'PASS'
    assert stress['compilerSha256'] == compiler
    assert board['cases'] == board['expectedCases'] == 768 and not board['mismatches']
    ide = []
    evidence = distribution/'docs/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    for folder in ('ide-ai-register-fixed', 'ide-ai-library', 'ide-stc-register'):
        result = read(work/folder/'result.json')
        assert result['success'] and result['report']['exitCode'] == 0
        receipt = read(work/folder/'project/.build/mx251/latest/receipt.json')
        assert receipt['compilerSha256'] == compiler
        ide.append(dict(device=result['device'], template=result['template'],
            codeBytes=receipt['codeBytes'], firmwareSha256=receipt['firmwareSha256'],
            result=pin(work/folder/'result.json')))
    for name in ('user-stress/compilation.json', 'user-stress/host-result.json',
                 'hardware-user-stress/result.json', 'portable-smoke/results.json'):
        target = evidence/name.replace('/', '-')
        shutil.copy2(work/name, target)
    qualification = dict(schema=1, version='0.180', status='PASS',
        date=datetime.now().astimezone().isoformat(timespec='seconds'),
        compilerSha256=compiler, compilerSourceCommit=audit['candidate'].get('sourceCommit',
            read(distribution/'core/core.json')['compilerSourceCommit']),
        matrix=dict(projects=203, built=203, reusedIdenticalBuilds=matrix['summary']['reused'],
            completion=pin(work/'matrix-final/completion.json'), supplementalBuilds=supplements,
            comparisons=comparisons),
        portableExamples=portable, studioxTemplates=ide,
        studioxScope='Actual ProjectService and BuildService; final archive import is recorded in delivery-validation.json',
        independentUserStress={k: stress[k] for k in ('status', 'configurations', 'translationUnits',
            'targets', 'identicalPureCObjects', 'valueLimit', 'nodeLimit')},
        hardware=dict(target='AI8051U experiment box', status='PASS', uniqueCases=768,
            timingQualified=False, evidence='evidence/hardware-user-stress-result.json'),
        fullProjectRuntimeMeasured=False, keilRuntime110Qualified=False,
        limitations=['13 closed Keil OMF-library projects excluded',
            'g140 original-input failure excluded', '74 assembly-only examples not rebuilt',
            'p144, p145 and g083 are raw comparisons and excluded from threshold counts',
            'Size thresholds and board semantic checks do not qualify runtime against Keil'])
    save(work/'acceptance.json', qualification)
    save(distribution/'docs/qualification.json', qualification)
    save(distribution/'docs/comparison.json', comparison)
    if repo_output:
        save(REPO/'docs/QUALIFICATION-v0.180.json', qualification)
        save(REPO/'docs/COMPARISON-v0.180.json', comparison)
    lines = ['# MX251 v0.180 验收结果', '',
        '203/203 个冻结 C 工程构建通过；包含 AI8051U 106 项和 STC32G 97 项。',
        '25 项内容一致的同编译器结果直接复用；其余工程使用发行包入口完成。', '',
        '| 参照 | 可比较工程 | ≤110% | ≤120% |', '| --- | ---: | ---: | ---: |']
    for ref, label in [('keil', 'Keil C251'), ('stc-soft', 'STC-IDE')]:
        c = comparisons[ref]
        lines.append(f"| {label} | {c['comparable']} | {c['at110']} | {c['at120']} |")
    lines += ['', '数字比较的是 HEX 有效载荷体积，运行时间目标尚未完成验收。', '',
        '- 独立包三个示例通过；MCU_StdioX 的 AI8051U 寄存器、AI8051U 库函数、STC32G 寄存器三个模板通过。',
        '- 独立用户压力集 9 种配置、63 个编译单元通过；三个目标描述的纯 C 对象一致。',
        '- AI8051U 实板完成 768 个不同语义检查，全部通过。',
        '- 13 个闭源 OMF 库工程、g140 原始输入失败及 74 个汇编例程未包含在本组验收。',
        '- p144、p145、g083 保留原始数据，因数学语义/模型差异不纳入体积过线分母。', '',
        '## 相对上一版的体积变化', '']
    c = comparison['previousCompilerComparison']
    lines.append(f"200 项常规比较中，{c['smaller']} 项缩小、{c['unchanged']} 项不变、{c['larger']} 项增大，总变化 {c['netByteChange']:+d} B。")
    lines += ['', '以下列出增大的工程；发行版本保留这些已知取舍，后续集中优化。', '',
        '| 工程 | 本版 B | 变化 B | Keil 比例 |', '| --- | ---: | ---: | ---: |']
    for row in sorted((r for r in normal if r['delta'] > 0), key=lambda r: -r['delta']):
        lines.append(f"| {row['id']} | {row['bytes']} | +{row['delta']} | {row['keilPercent']:.2f}% |")
    lines += ['', '完整逐工程数据见 `comparison.json`，测试身份和范围见 `qualification.json`。',
              '编译器 SHA-256：`'+compiler+'`。', '']
    report = '\n'.join(lines)
    (distribution/'docs/QUALIFICATION-v0.180.md').write_text(report, encoding='utf-8', newline='\n')
    if repo_output:
        (REPO/'docs/QUALIFICATION-v0.180.md').write_text(report, encoding='utf-8', newline='\n')
    print(comparisons)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--distribution', type=Path, required=True)
    p.add_argument('--report-only', action='store_true')
    args = p.parse_args()
    main(args.work, args.distribution, not args.report_only)
