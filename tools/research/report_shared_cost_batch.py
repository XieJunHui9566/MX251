"""Summarize one frozen shared-cost/address/ISR batch against its frozen parent."""
import argparse
from pathlib import Path

from full_matrix_report import summary
from report_frozen_candidate_matrix import attribute_growth, compare_functions
from unified_sdk_matrix import pin, read, save


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); root = a.root
    audit = read(root/'batch-audit.json')
    result = read(root/'full-matrix/results.json')
    completion = read(root/'full-matrix/completion.json')
    assert completion['results']['sha256'] == pin(root/'full-matrix/results.json')['sha256']
    assert result['summary']['completed'] == result['summary']['total'] == 203
    candidate = audit['candidate']; compiler = candidate['compilerSha256']
    for item in candidate['contents']:
        assert pin(Path(candidate['root'])/item['path'])['sha256'] == item['sha256']
    old = {x['id']: x for x in audit['historical']['projects']}
    inputs = {x['id']:x for x in read(audit['inventory']['frozenInputs']['path'])['projects']}
    source_count = 0; stdio = []; reuse = 0
    for row in result['rows']:
        row['before'] = row['previousBytes']
        project = Path(row['result']['path']).parent
        for item in inputs[row['id']]['files']:
            assert pin(project/item['path'])['sha256'] == item['sha256']
            source_count += 1
        if row['status'] != 'BUILT': continue
        receipt = read(row['receipt']['path']); latest = Path(row['receipt']['path']).parent
        assert receipt['compilerSha256'] == compiler and receipt['codeBytes'] == row['bytes']
        assert receipt['firmwareSha256'] == row['hex']['sha256']
        choice = read(latest/'stdio-sharing.json')
        stdio.append(dict(id=row['id'], **choice))
        reuse += sum('compileReuse' in x for x in read(latest/'commands.json'))
    rows = result['rows']; by_id = {x['id']: x for x in rows}
    normal = [r for r in rows if not r['rawComparisonOnly']]
    groups = {}
    for cohort in sorted({r['cohort'] for r in normal}):
        group = [r for r in normal if r['cohort'] == cohort]
        groups[cohort] = {ref:summary(group,ref) for ref in sorted(group[0]['references'])}
    groups['all'] = {'keil':summary(normal,'keil')}
    increases = [r for r in rows if r['status']=='BUILT' and r['delta']>0]
    gains = sorted([r for r in rows if r['status']=='BUILT' and r['delta']<0],key=lambda r:r['delta'])
    hardware = []
    for name, project in [('hardware-final-small','board-final-small'),
                          ('hardware-final-large','board-final-large'),('hardware-isr','board-isr')]:
        record = read(root/name/'result.json'); latest = root/project/'.build/mx251/latest'
        assert record['status']=='PASS' and record['cases']==128
        assert record['firmwareSha256']==pin(latest/'firmware.hex')['sha256']
        assert read(latest/'receipt.json')['compilerSha256']==compiler
        hardware.append(dict(fixture=project,result=pin(root/name/'result.json'),
                             cases=record['cases'],frames=record['frames'],firmware=pin(latest/'firmware.hex')))
    focus_ids = ['p083','p123','g104','p127','g139','g071','p092','p140','g141']
    focus = [by_id[pid] for pid in focus_ids]
    reuse_check=read(root/'reuse-check/result.json')
    assert reuse_check['status']=='PASS' and reuse_check['compilerSha256']==compiler
    compiler_check=read(root/'compiler-check-qualified/result.json')
    assert compiler_check['status']=='PASS' and compiler_check['compilerSha256']==compiler
    data = dict(schema=1,date='2026-11-16',candidate=candidate,implementationCommit=candidate['sourceCommit'],
        status=completion['status'],run=pin(root/'full-matrix/run.json'),completion=pin(root/'full-matrix/completion.json'),
        baselineReport=pin(Path(__file__).resolve().parents[2]/'docs/FROZEN-MATRIX-20261116.json'),
        summary=groups,buildSummary=result['summary'],sourceFilesChecked=source_count,
        sourceInputsIdentical=True,sdkSourcesChanged=False,defaultChanged=False,
        hardware=hardware,hardwareCases=sum(x['cases'] for x in hardware),
        compilerCheck=pin(root/'compiler-check-qualified/result.json'),
        compileReuseCheck=pin(root/'reuse-check/result.json'),
        stdioChoices=stdio,stdioSavedBytes=sum(x['savedBytes'] for x in stdio),
        selectedCompileReuseHits=reuse,focus=focus,
        largestGains=[r['id'] for r in gains],increases=[r['id'] for r in increases],
        growthAttribution=[attribute_growth(r,old[r['id']]) for r in increases],
        functionComparisons=[compare_functions(by_id[pid]) for pid in ('p127','g139','g071','p140')],
        fullMatrixHardwareExecuted=False,keilRuntimeQualification='PENDING',
        excluded=audit['inventory']['excluded'],limitedComparisons=audit['inventory']['limitedComparisons'],rows=rows)
    save(a.output.with_suffix('.json'),data)
    total=groups['all']['keil']
    lines=['# MX251 共享库与通用后端优化批次 · 2026-11-16','',
        f"实现提交 `{candidate['sourceCommit'][:7]}`；同一冻结候选完成 {result['summary']['built']}/203 项构建。",
        '与上一轮冻结编译器使用相同应用源码、配置和 Keil/STC-IDE 参考；不修改 SDK，不更换默认核心。','',
        '## 整批结果','',
        f"正常可比200项：Keil 110% 为 {total['at110']}/200，120% 为 {total['at120']}/200。",
        f"较上一冻结版本，{total['smaller']} 项缩小、{total['larger']} 项增大、{total['unchanged']} 项不变；正常集合合计变化 {total['afterBytes']-total['beforeBytes']:+d} B。",
        f"新进入110%：{', '.join(total['new110']) or '无'}；退出110%：{', '.join(total['lost110']) or '无'}。",
        f"新进入120%：{', '.join(total['new120']) or '无'}；退出120%：{', '.join(total['lost120']) or '无'}。",'',
        '| 集合 | 对照 | 可比数 | ≤110% | ≤120% |','| --- | --- | ---: | ---: | ---: |']
    for cohort, refs in groups.items():
        if cohort=='all': continue
        for ref,s in refs.items():lines.append(f"| {cohort} | {ref} | {s['compared']} | {s['at110']} | {s['at120']} |")
    lines += ['', '## 代表工程','', '| 工程 | 上版/B | 本版/B | 变化/B | Keil/B | 本版/Keil |',
              '| --- | ---: | ---: | ---: | ---: | ---: |']
    for r in focus:
        lines.append(f"| {r['id']} | {r['before']} | {r.get('bytes','失败')} | {r.get('delta','—')} | {r.get('keilBytes','—')} | {r.get('keilPercent',0):.2f}% |")
    lines += ['', '## 通用改动','',
        '- 共享输出库：将 printf→puts 的取舍推迟到完整链接后，比较两种完整固件，保留更小版本。只对同时链接标准 puts/printf 且存在自动改写的工程试编。',
        f"- 构建复用：源码与头文件、工具及配置锁、参数、反馈内容相同的编译调用在同次构建内复用。最终所选构建日志记录 {reuse} 次复用；不引入跨项目持久缓存。",
        '- CODE 索引：将常量表的符号地址保留到访存发射，减少循环中的地址活值和栈搬运，保持原读取空间与宽度。',
        '- 指针对象：常量地址直接按三字节布局写入，普通写入使用字指令，volatile 写入保持原来的逐字节顺序。',
        '- 位与中断：相邻的单用位逻辑更新直接读写进位位，保留端口读行为；把间接写入的原始 IR 信息交给现有保存分析。未知地址和核心寄存器别名继续保存完整上下文。',
        '- 浮点调用：识别编译器自身的 binary32 运行库符号，复用已有 ABI 参数/返回值传递，并允许完整函数比较后端成本。支持能力不等于强制选用较大实现。','',
        '## 验证范围','',
        '- 22 个定向编译用例通过，覆盖位取反及复用结果、不同目标位、插入的真实写入、已知/未知/核心/跨地址边界存储、普通浮点运行库调用、volatile 指针和自引用指针写入。',
        '- 多文件输出程序的复用构建与空目录重新编译得到逐字节相同的最终 HEX。',
        '- AI8051U 实板：small/large 独立浮点与表/指针测试各128项；真实中断抢占测试128项，覆盖四组寄存器银行、DPX、位状态及已知地址写入。合计384项通过。',
        '- 中断最终 HEX 与已执行版本逐字节相同，复用该实板结果；先前触发旧后端的测试版本不计入新指针优化的实板覆盖。',
        '- 203个官方工程为编译、链接和体积验收，未逐项执行；与 Keil 的运行时间110%要求仍未完成。',
        '- p144/p145/g083 的比较限制、OMF依赖和纯汇编例程范围沿用上一轮，未混入正常200项。','',
        '## 遗留体积差距','',
        '| 工程 | 本版/B | Keil/B | 比例 | 距120%/B |','| --- | ---: | ---: | ---: | ---: |']
    for r in sorted([r for r in normal if r['status']=='BUILT' and r['gap120']],key=lambda r:-r['gap120']):
        lines.append(f"| {r['id']} | {r['bytes']} | {r['keilBytes']} | {r['keilPercent']:.2f}% | {r['gap120']} |")
    lines += ['', f"完整结果见 [{a.output.stem}.json]({a.output.stem}.json)。冻结候选与运行日志：`{root.as_posix()}`。",'']
    a.output.with_suffix('.md').write_text('\n'.join(lines),encoding='utf-8',newline='\n')
    print(dict(status=data['status'],at110=total['at110'],at120=total['at120'],
               delta=total['afterBytes']-total['beforeBytes'],increases=data['increases'],hardwareCases=data['hardwareCases']))


if __name__=='__main__': main()
