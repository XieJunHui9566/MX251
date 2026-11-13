"""Summarize every attempted SDK project without dropping unsuccessful inputs."""
from pathlib import Path
import argparse
import collections
import json
import re
import sdk_coverage_matrix as matrix


def diagnostics(folder, result):
    lines=[]
    for p in sorted((folder/'.build/mx251/latest').glob('*.stderr.log')):
        text=p.read_text('utf-8',errors='replace')
        for line in text.splitlines():
            if re.search(r'error \d+:|syntax error:|ASlink-Error|Undefined Global',line):
                lines.append(dict(log=matrix.pin(p),message=line))
    if not lines and result['status']!='BUILT':
        error=result.get('error',result.get('reason',''))
        matches=[line for line in error.splitlines() if re.search(r'\*\*\* ERROR|HEX overlap|Runtime .* qualified|Unclassified pragma|ISR number',line)]
        lines=[dict(message=line) for line in matches] or [dict(message=error[:700])]
    return lines[:5]


def pick(attempts, order):
    for key in order:
        if key in attempts and attempts[key]['status']=='BUILT':return attempts[key]
    for key in order:
        if key in attempts:return attempts[key]
    return None


def main(root, repo):
    inventory=matrix.read(root/'inventory.json')
    inputs=matrix.read(root/'inputs.json')
    baseline=matrix.read(Path(inputs['configuration']['output']).parents[1]/'comparison.json')
    rows=[]
    for project in inputs['projects']:
        attempts={}
        for scope in ('','header-supplement'):
            base=root/scope/'projects'/project['id']
            for f in sorted(base.glob('*/result.json')):
                result=matrix.read(f)
                key=('header/' if scope else '')+f.parent.name
                attempts[key]=dict(suite=key,status=result['status'],bytes=result.get('payloadBytes'),
                    model=result.get('model'),result=matrix.pin(f),diagnostics=diagnostics(f.parent,result))
        assert all(s in attempts for s in ('mx251','keil','stc-soft','stc-mdu32')),project['id']
        mx=pick(attempts,['header/mx251','mx251-large','mx251-edata','mx251'])
        keil=pick(attempts,['header/keil-symbols','header/keil-extended','keil-extended','header/keil-fixed','keil-fixed','header/keil','keil-asm','keil'])
        native=pick(attempts,['mx251-native'])
        stc=pick(attempts,['header/stc-soft','stc-soft'])
        mdu=pick(attempts,['header/stc-mdu32','stc-mdu32'])
        row=dict(id=project['id'],name=project['name'],group=project['group'],cSources=project['cSources'],
                 attempts=attempts,mx=mx,keil=keil,native=native,stcSoft=stc,stcHardware=mdu,
                 sourceManifest=project['sourceManifest'],libraries=project['libraries'])
        for label, candidate, reference in [('mxKeilPercent',mx,keil),('nativeKeilPercent',native,keil),
                                           ('mxStcSoftPercent',mx,stc),('nativeStcSoftPercent',native,stc)]:
            row[label]=100*candidate['bytes']/reference['bytes'] if candidate and reference and candidate['bytes'] and reference['bytes'] else None
        rows.append(row)
    crows=[r for r in rows if r['cSources']]
    arows=[r for r in rows if not r['cSources']]
    summary=dict(rebuiltCProjects=len(crows),assemblyProjects=len(arows),
        mxCWithHex=sum(r['mx']['status']=='BUILT' for r in crows),
        mxDefaultCWithHex=sum(r['attempts']['mx251']['status']=='BUILT' for r in crows),
        mxOmfUnsupported=sum(bool(r['libraries']) for r in crows),
        keilCWithHex=sum(r['keil']['status']=='BUILT' for r in crows),
        stcSoftwareCStatuses=dict(collections.Counter(r['stcSoft']['status'] for r in crows)),
        keilAssemblyWithHex=sum(r['keil']['status']=='BUILT' for r in arows),
        priorFrozenCWithHex=len(baseline['rows']),
        combinedHistoricalAndCurrentCWithHex=len(baseline['rows'])+sum(r['mx']['status']=='BUILT' for r in crows))
    result=dict(schema=1,sourceDate='2026-11-13',summary=summary,inventory=inventory,rows=rows,
        prior75=matrix.pin(Path(inputs['configuration']['output']).parents[1]/'comparison.json'),
        compiler=matrix.pin(Path(inputs['configuration']['core'])/'toolchain/bin/sdcc.exe'),
        originalManifest=matrix.pin(root/'inputs.json'),nativeCore=matrix.read(root/'native-core-provenance.json'),
        stcVendorManifest=matrix.pin(root/'stc-official/vendor-manifest.json'),
        tools=[matrix.pin(Path(__file__).with_name(n)) for n in ('sdk_coverage_matrix.py','sdk_coverage_followups.py','unified_sdk_matrix.py','report_sdk_coverage.py')],
        measurement='Unique Intel HEX payload bytes, excluding holes; whole firmware including constants/startup/vectors',
        hardwareExecuted=False,runtimeMeasured=False,
        qualification='Compile coverage only. Large-model fallbacks and comparison-only native-math are explicit variants. Previous 75 retain their earlier frozen compiler; no new unified 114-project claim.')
    matrix.save(root/'coverage-summary.json',result)
    matrix.save(repo/'docs/SDK-MATH-COVERAGE.json',result)
    def value(item):return str(item['bytes']) if item and item['status']=='BUILT' else '—'
    lines=['# 官方 AI8051U 例程补跑与数学差距','',
        '2026-11-13。当前编译器源提交 `53baf2a`；本轮仅修改批量对照工具和记录，未修改编译器实现与 SDK 算法。','',
        '## 范围与完成情况','',
        '[官方实验箱页面](https://stcai.com/syx) 仍发布 V1.2／20261027 包；归档共 258 个工程：149 个 32 位工程、109 个 8 位工程。149 个中含 114 个 C 工程和 35 个纯汇编工程。8 位例程不混入 MX251/C251 对照。官网页面、下载包响应头与归档哈希保存在本轮证据目录。','',
        f'此前已建立 75 项冻结基线，本轮补跑其余全部 **39 个 C 工程**，并记录全部 **35 个纯汇编工程**。39 项中 MX251 得到 **{summary["mxCWithHex"]} 个 HEX**：原配置 5 项、large 模型 7 项、补入官方公共头文件 2 项；11 项依赖 Keil OMF 二进制库，其余 14 项仍失败。','',
        f'Keil C251 得到 **{summary["keilCWithHex"]}/39** 个 C 工程 HEX、**{summary["keilAssemblyWithHex"]}/35** 个纯汇编 HEX。STC-IDE 软件模式得到 17 个 C 工程 HEX，9 项不支持原汇编输入，11 项不支持 Keil OMF，2 项缺函数/输出入口。MX251 的 ASxxxx 汇编器不能直接接收这 35 个 A251 方言工程，STC worker 也不接收它们；没有把它们算成 C 优化失败。','',
        f'按已有产物覆盖汇总为 **{summary["combinedHistoricalAndCurrentCWithHex"]}/114** 个 C 工程有 MX251 HEX。这里包含上一版冻结的 75 项及本轮新增 14 项，不能表述成“同一新版编译器通过 89 项”。本轮没有完整工程上板功能/耗时对比。','',
        '## 数学九项','',
        '单位：HEX 有效字节。默认列使用当前发布配置的 standard 运行库；新版数学库列来自独立对照副本，仅加入 AI8051U ordinary native-math 准入并更新配置哈希，编译器、库、启动和 SDK 源码不变。该副本未升级正式支持状态。STC 软件与硬件数学模式分列。','',
        '| ID | 默认 MX251 | 新数学库 | Keil | STC 软件 | STC 硬件数学 | 新数学库/Keil |',
        '|---|---:|---:|---:|---:|---:|---:|']
    for r in crows:
        if r['group']=='math9':
            ratio=f'{r["nativeKeilPercent"]:.2f}%' if r['nativeKeilPercent'] else '—'
            lines.append(f'| {r["id"]} | {value(r["mx"])} | {value(r["native"])} | {value(r["keil"])} | {value(r["stcSoft"])} | {value(r["stcHardware"])} | {ratio} |')
    lines += ['',
        '5 项生成 HEX，3 项受 OMF 库阻碍（p093/p094/p127），p130 在字库聚合初始化处失败。已尝试既有显式花括号适配器，该输入不在其支持形状内，没有扩大改写。5 项默认配置及新版数学库配置均未到 Keil 120%。','',
        '新版数学库使 p041 从 10289 降到 8446 B（−17.91%），p081 从 13702 降到 9106 B（−33.54%）。p076 增加 155 B、p079 增加 225 B、p077 减少 114 B，因此不能把新数学库直接称为所有工程的最优配置。与 11 月 1 日旧编译器比较，p076 默认配置从 49368 降到 25715 B，p077 从 49405 降到 33587 B，p079 从 49277 降到 33215 B；这些包含之前通用优化的累计收益。','',
        '## 本轮全部 C 工程','',
        'MX 列优先保留成功的已记录配置，large 与头文件补齐明确标注。large 对照不是同内存模型验收。失败项没有从分母剔除。','',
        '| ID | MX251/B | 配置/状态 | Keil/B | STC 软件/B | MX/Keil |',
        '|---|---:|---|---:|---:|---:|']
    for r in crows:
        ratio=f'{r["mxKeilPercent"]:.2f}%' if r['mxKeilPercent'] else '—'
        lines.append(f'| {r["id"]} | {value(r["mx"])} | {r["mx"]["suite"]}: {r["mx"]["status"]} | {value(r["keil"])} | {value(r["stcSoft"])} | {ratio} |')
    lines += ['','## 仍缺什么','',
        '- 11 项 Keil OMF 二进制库：p083、p093、p094、p118、p124、p125、p126、p127、p140、p147、p148。需要库源码/可用替代实现或 ABI/格式兼容，单纯压缩生成代码不能解决。',
        '- p022：普通自动局部变量指定 edata 与 stack-auto/reentrant 的限制。p135/p141 补上 edata 方言拼写后还出现逗号处语法错误，已留日志；没有把 edata 映射到其它存储区。',
        '- p070/p106/p120/p121/p123：链接缺入口，如 QSPI_Init、putchar 或 _getkey。',
        '- p092：CODE 描述符重复定义诊断；p105/p115：large 模型链接后的 HEX 地址重叠/布局问题；p130：字库聚合初始化。',
        '- p144/p145：补齐官方头文件后仍存在类型、参数或函数属性问题。Keil p144 也在 putchar 属性处失败；p145 的 Keil 参考已生成。','',
        '## 下一步优先级','',
        '1. 将普通 C 的软件浮点库接入 AI8051U 构建入口，并把浮点格式化输出纳入同一轮研究。p041/p081 仍拉入完整 printf_large；只优化独立 Math123 不足以补齐实际 SDK。',
        '2. 对齐 Keil XSMALL 的默认 EDATA 布局能力。7 个工程切到 large 就能生成 HEX，说明共同差距来自数据布局和地址模型；这里比继续逐条缩减算术指令更值得做。',
        '3. 分别处理上述源码兼容、链接与二进制库入口；保持所有 114 个 C 工程在总账中。','',
        '## 复现与证据','',
        f'完整输出：`{root.as_posix()}`。入口为 `sdk_coverage_matrix.py --config .../config.json --jobs 6`，补测脚本支持 native、large、headers、edata、braces、keil、assembly、extended-vectors、vector-case。每次尝试保留独立目录、命令、输入摘要和结果。','',
        '对照工具补齐了 OMF 库链接列表、A251 include 文件、公共头文件、Keil 最高 65 号向量、调用约定 pragma 及实际导出符号。所有参考都由本机工具新生成；未改官方 worker。补测副本保留原输入失败记录。','',
        '[结构化总账](SDK-MATH-COVERAGE.json) 包含全部 149 个工程清单及本轮每次结果的文件哈希。','']
    report='\n'.join(lines)
    (root/'REPORT.md').write_text(report,encoding='utf-8',newline='\n')
    (repo/'docs/SDK-MATH-COVERAGE.md').write_text(report,encoding='utf-8',newline='\n')
    print(json.dumps(summary,ensure_ascii=False,indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--repo',type=Path,required=True)
    args=ap.parse_args()
    main(args.root,args.repo)
