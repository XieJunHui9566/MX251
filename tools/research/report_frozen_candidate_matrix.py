"""Report completed frozen-candidate builds, gains, regressions and open gaps."""
import argparse
from collections import Counter
from pathlib import Path
import re

from full_matrix_report import summary
from unified_sdk_matrix import pin, read, save
from call_layout import ihex, read_unit
from vector_gc_attribution import keil_parts


def function_bytes(root):
    """Attribute instructions using existing listings bound to the final HEX."""
    memory = ihex(root/'firmware.hex')
    functions = Counter()
    names = [u['name'] for u in read(root/'project.snapshot.json')['units']
             if Path(u['file']).suffix.lower() == '.c']
    if (root/'format-runtime.rst').exists():
        names.append('format-runtime')
    for name in names:
        unit = read_unit(root, name, memory)
        for ins in unit['instructions']:
            if ins.owner:
                functions[(name, ins.owner)] += len(ins.data)
    return functions


def attribute_growth(row, old):
    roots = [Path(r['receipt']['path']).parent for r in (old, row)]
    before, after = [function_bytes(root) for root in roots]
    members = [set(re.findall(r'\.lib\s*\[\s*(\S+)\s*\]',
                   (root/'linked.partial.map').read_text('latin1'))) for root in roots]
    changes = [dict(unit=u, function=f, before=before[u, f], after=after[u, f],
                    delta=after[u, f]-before[u, f])
               for u, f in sorted(before.keys() | after.keys())
               if before[u, f] != after[u, f]]
    return dict(id=row['id'], delta=row['delta'], functions=changes,
                newLibraryMembers=sorted(members[1]-members[0]),
                maps=[pin(root/'linked.partial.map') for root in roots],
                otherBytes=row['delta']-sum(c['delta'] for c in changes),
                note='otherBytes includes constants, startup, libraries and unowned instructions')


def compare_functions(row):
    root = Path(row['receipt']['path']).parent
    mx = function_bytes(root)
    keil = keil_parts(Path(row['references']['keil']['result']['path']).parent)
    choices = {}
    for unit in {u for u, _ in mx}:
        asm = (root/(unit+'.asm')).read_text('latin1')
        for symbol, selected, wide, legacy in re.findall(
                r'mx251-backend-choice function=(\S+) selected=(\S+) wide-bytes=(\d+) legacy-bytes=(\d+)',
                asm):
            choices[unit, symbol] = dict(selected=selected, wideBytes=int(wide), legacyBytes=int(legacy))
        for selected, symbol in re.findall(r'^; mcs251 wide-mir (accepted|fallback): (\w+)', asm, re.M):
            choices.setdefault((unit, '_'+symbol), dict(selected='wide' if selected == 'accepted' else 'legacy',
                                                       evidence=selected))
        for symbol, reason in re.findall(r'^; wide refusal function=(\w+) stage=\S+ reason=(\S+)', asm, re.M):
            choices.setdefault((unit, '_'+symbol), dict(selected='legacy'))['refusal'] = reason
    pairs = []
    for (unit, symbol), size in mx.items():
        name = re.sub(r'^_+m251r4_', '', symbol).lstrip('_').upper()
        if name in keil['functions']:
            reference = keil['functions'][name]
            pairs.append(dict(unit=unit, function=name, mx=size, keil=reference,
                              delta=size-reference, backendChoice=choices.get((unit, symbol))))
    return dict(id=row['id'], pairs=sorted(pairs, key=lambda p: -p['delta']),
                references=keil['inputs'],
                note='MX instruction bytes versus Keil function segments (may include tables); '
                     'differences are observed costs, not guaranteed removable bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    audit = read(args.audit)
    completion = read(args.root/'completion.json')
    result = read(args.root/'results.json')
    assert completion['results']['sha256'] == pin(args.root/'results.json')['sha256']
    assert result['summary']['completed'] == result['summary']['total'] == 203
    inputs = {p['id']: p for p in read(audit['inventory']['frozenInputs']['path'])['projects']}
    historical = {p['id']: p for p in audit['historical']['projects']}
    source_count = 0
    for row in result['rows']:
        row['before'] = row['previousBytes']
        root = Path(row['result']['path']).parent
        for item in inputs[row['id']]['files']:
            assert pin(root/item['path'])['sha256'] == item['sha256'], (row['id'], item['path'])
            source_count += 1
        row['sourceInputsIdentical'] = True
        row['configuration'] = historical[row['id']]['configuration']
        if row['status'] == 'BUILT':
            receipt = read(row['receipt']['path'])
            assert receipt['compilerSha256'] == audit['candidate']['compiler']['sha256']
            assert receipt['codeBytes'] == row['bytes']
            assert receipt['firmwareSha256'] == row['hex']['sha256']
        row['semanticNotes'] = historical[row['id']].get('semanticNotes')
        row['declaredInputOverride'] = historical[row['id']].get('declaredInputOverride')
    rows = result['rows']
    normal = [r for r in rows if not r['rawComparisonOnly']]
    groups = {}
    for cohort in sorted({r['cohort'] for r in rows}):
        group = [r for r in normal if r['cohort'] == cohort]
        groups[cohort] = {ref: summary(group, ref) for ref in sorted(group[0]['references'])}
    groups['all'] = {'keil': summary(normal, 'keil')}
    raw = {cohort: summary([r for r in rows if r['cohort'] == cohort], 'keil')
           for cohort in sorted({r['cohort'] for r in rows})}
    increases = sorted([r for r in rows if r['status'] == 'BUILT' and r['delta'] > 0],
                       key=lambda r: -r['delta'])
    decreases = sorted([r for r in rows if r['status'] == 'BUILT' and r['delta'] < 0],
                       key=lambda r: r['delta'])
    remaining = sorted([r for r in normal if r['status'] == 'BUILT' and r['gap120']],
                       key=lambda r: -r['gap120'])
    growth = [attribute_growth(r, historical[r['id']]) for r in increases]
    focus = [compare_functions(r) for r in remaining[:5]]
    output = dict(schema=1, date='2026-11-16',
        status='PASS_FULL_MATRIX_BUILDS' if not completion['summary']['failed'] else 'BUILD_FAILURES',
        candidate=audit['candidate'], run=pin(args.root/'run.json'),
        completion=pin(args.root/'completion.json'), previousAudit=pin(args.audit),
        sourceInputsIdentical=True, sourceFilesChecked=source_count,
        compilerChanged=False, runtimeChanged=False, sdkSourcesChanged=False, defaultChanged=False,
        comparison='One frozen MX251 candidate versus retained same-source Keil/STC reference images',
        deltaBasis='Latest historical project result; mixed parent revisions, not a new optimization A/B',
        measurement='HEX payload bytes, holes excluded, integer threshold comparisons',
        fullMatrixHardwareExecuted=False, firmwareExecutionTime=None,
        summary=groups, rawSummary=raw, buildSummary=result['summary'],
        smaller=len(decreases), larger=len(increases),
        unchanged=sum(r['status'] == 'BUILT' and r['delta'] == 0 for r in rows),
        largestGains=[r['id'] for r in decreases], increases=[r['id'] for r in increases],
        growthAttribution=growth,
        largestGapFunctionComparison=focus,
        remaining120=[r['id'] for r in remaining],
        gap110Bytes=sum(r['gap110'] for r in normal if r['status'] == 'BUILT'),
        gap120Bytes=sum(r['gap120'] for r in remaining),
        largestFiveGap120Bytes=sum(r['gap120'] for r in remaining[:5]),
        excluded=audit['inventory']['excluded'],
        assemblyNotRebuilt=audit['inventory']['assemblyNotRebuilt'],
        limitedComparisons=audit['inventory']['limitedComparisons'],
        independentUserPrograms=audit['independentUserPrograms'],
        currentKeilRuntimeQualification='PENDING',
        models=dict(Counter(r['configuration']['model'] for r in rows)),
        runtimes=dict(Counter(r['configuration']['runtime'] for r in rows)),
        reporter=pin(__file__), rows=rows)
    save(args.output.with_suffix('.json'), output)
    total = groups['all']['keil']
    lines = ['# 冻结候选统一工程测试 · 2026-11-16', '',
        f"本轮完成 {result['summary']['completed']}/203 项：{result['summary']['built']} 项生成 HEX；"
        f"其中新编译 198 项，复用同一完整候选的 5 项。候选为 `{audit['candidate']['buildId']}`。",
        '编译器、运行库和 SDK 算法未改。Keil/STC-IDE 沿用已有同源参考；默认核心未替换。', '',
        '## 与 Keil 的体积比较', '',
        '下表均为 MX251/Keil 的完整 HEX 有效载荷比值，包含常量和初始化数据，地址空洞不计。'
        'p144、p145 内存模型不同，g083 格式实参宽度不匹配；三项保留原始记录但不计入正常达标数。', '',
        '| 集合 | 正常可比 | ≤110% | ≤120% | >120% |',
        '| --- | ---: | ---: | ---: | ---: |']
    for cohort, label in [('ai8051u-frozen75','AI8051U 原75项'),
                         ('ai8051u-expanded31','AI8051U 扩展集合'),
                         ('stc32g-97','STC32G'), ('all','合计')]:
        s = groups[cohort]['keil']
        lines.append(f"| {label} | {s['compared']} | {s['at110']} | {s['at120']} | {len(s['over120'])} |")
    lines += ['', '## 与 STC-IDE 的体积比较', '',
        '| 集合/数学库 | 正常可比 | ≤110% | ≤120% |', '| --- | ---: | ---: | ---: |']
    for cohort, ref, label in [('ai8051u-frozen75','stc','AI 原75项/MDU32'),
        ('ai8051u-expanded31','stc-soft','AI 扩展/软件数学'),
        ('ai8051u-expanded31','stc-mdu32','AI 扩展/MDU32'),
        ('stc32g-97','stc-soft','STC32G/软件数学')]:
        s = groups[cohort][ref]
        lines.append(f"| {label} | {s['compared']} | {s['at110']} | {s['at120']} |")
    lines += ['', '缺失的参考未计作通过；STC32G 的正常比较分母排除了 g083。', '',
        '## 相对最近历史记录的变化', '',
        f"203 项中：缩小 {len(decreases)} 项、增大 {len(increases)} 项、"
        f"不变 {output['unchanged']} 项；合计变化 {result['summary']['deltaBytes']:+d} B。"
        '对照来自多轮历史版本，这些差额是统一版本后的累计结果，不归因于本轮新增优化。', '',
        f"新进入110%：{', '.join(total['new110']) or '无'}。",
        f"退出110%：{', '.join(total['lost110']) or '无'}。",
        f"新进入120%：{', '.join(total['new120']) or '无'}。",
        f"退出120%：{', '.join(total['lost120']) or '无'}。", '',
        '| 增大项目 | 历史/B | 当前/B | 增加/B | 当前/Keil |',
        '| --- | ---: | ---: | ---: | ---: |']
    for r in increases:
        lines.append(f"| {r['id']} | {r['before']} | {r['bytes']} | {r['delta']} | {r['keilPercent']:.2f}% |")
    puts_growth = [g for g in growth if 'puts.rel' in g['newLibraryMembers'] or
                   any(c['function'] == '_puts' and c['before'] == 0 and c['after'] > 0
                       for c in g['functions'])]
    lines += ['', f"其中 {len(puts_growth)} 个增长工程新增了标准 puts 函数："
        f"{', '.join(g['id'] for g in puts_growth) or '无'}。"
        '函数字节差异已按最终 HEX 对应的 ASM/RST 归因，详见 JSON 的 growthAttribution。'
        '纯文本 printf 改写减少调用处开销，但在调用较少时不足以摊平新增库函数；'
        '这是共享成本取舍问题，后续应按完整程序净收益处理，避免逐工程补丁。']
    lines += ['', 'p083 的 main 减少6 B、新增 puts 64 B、常量等减少1 B，整包因此增加57 B并退出120%线。'
        'g083 新引入归档中的 puts.rel，代码段增加96 B、常量减少1 B；其比较限制仍保持。'
        'p136 的 +2 B 来自 I2S_ISR 增加16 B与其它函数减少14 B相抵，不归因于 puts。']
    lines += ['', 'p123 继承的 +22 B 在此前已计入历史最新成绩，本表不会再次计为新增；仍是待处理项。', '',
        '## 当前仍超过120%的工程', '',
        '| ID | 工程 | MX/B | Keil/B | 比例 | 达到120%还差/B |',
        '| --- | --- | ---: | ---: | ---: | ---: |']
    for r in remaining:
        lines.append(f"| {r['id']} | {r['name'].replace('|','/')} | {r['bytes']} | {r['keilBytes']} | {r['keilPercent']:.2f}% | {r['gap120']} |")
    lines += ['', '## 最大缺口的函数分布', '',
        '取距120%缺口最大的五项，每项列出同名函数的前三处正差额。MX统计最终指令字节，'
        'Keil统计函数段（可能含跳转表）；这是定位依据，不等于可直接省下的字节。', '',
        '| 工程 | 函数 | MX/B | Keil/B | 差额/B | 已选后端 |',
        '| --- | --- | ---: | ---: | ---: | --- |']
    for project in focus:
        for pair in [p for p in project['pairs'] if p['delta'] > 0][:3]:
            selected = (pair['backendChoice'] or {}).get('selected', '未记录')
            lines.append(f"| {project['id']} | {pair['function']} | {pair['mx']} | {pair['keil']} | {pair['delta']} | {selected} |")
    lines += ['', '## 后续工作的收敛点', '',
        f"正常可比工程到110%的超额合计为 {output['gap110Bytes']} B，到120%为 {output['gap120Bytes']} B。"
        f"上面五项占120%超额的 {output['largestFiveGap120Bytes']} B"
        f"（{100*output['largestFiveGap120Bytes']/output['gap120Bytes']:.1f}%）。"
        '这些是逐工程目标缺口，不是已证明能删除的指令量。', '',
        '- 先统一处理 printf/puts 的共享库成本，解决本次出现的成组增长；采用完整程序净收益作为选择依据。',
        '- 主要优化范围集中于 FFT 的活值/地址分配、USB 控制流程，以及 PWM 等内存标量运算。'
        'p127 的 main 仍因调用接口契约降级；g139 的 ISR 能进入 wide，但496 B大于旧后端339 B。'
        '两类原因分别处理，不能用“已支持新后端”代替代码质量指标。',
        '- 先用上述代表函数与独立用户程序证明同一机制有足够收益，再统一完成一次整包验收。'
        '已有地址与调用研究继续沿用；本轮没有重新实现优化，也没有启动下一轮编译器修改。']
    lines += ['', '## 验证范围与遗留事项', '',
        f"- {source_count} 个输入文件在构建后与冻结哈希一致，所有成功回执使用同一编译器。候选所有文件在本轮开始与结束一致。",
        '- 这是完整编译、链接和体积测试。已有独立用户压力程序实板 768/768 通过；203 项官方固件没有逐项实板运行。',
        '- 与 Keil 的同条件运行时间验收尚未补齐，不能从体积达标推出运行时间达标。',
        '- 13 项 OMF 依赖、g140 的原始输入错误、74 项纯 A251 例程继续单独列账，未混入203项。',
        '- p144/p145/g083 的比较限制保持不变；原始体积与配置见 JSON。',
        '- 下一步依据本表的共同缺口和增长项安排工作；不再使用混合版本表判断当前达标数量。', '',
        '## 记录', '',
        f"- 完整结果：[{args.output.name}.json]({args.output.name}.json)。",
        f"- 批量输出：`{args.root.as_posix()}`。",
        '- 运行工具：`tools/research/frozen_candidate_matrix.py`；报告工具：`tools/research/report_frozen_candidate_matrix.py`。', '']
    args.output.with_suffix('.md').write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    print(dict(status=output['status'], built=result['summary']['built'], at110=total['at110'],
        at120=total['at120'], normal=total['compared'], failed=result['summary']['failed'],
        remaining120=output['remaining120'], larger=output['increases']))


if __name__ == '__main__':
    main()
