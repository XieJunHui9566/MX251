"""Summarize the complete STC32G matrix without dropping unsupported projects."""
import argparse
import collections
from pathlib import Path
import re
import statistics
import unified_sdk_matrix as m


def compare(rows, reference):
    pairs = [r for r in rows if r['cSources'] and all(r['results'][s]['status']=='BUILT' for s in ('mx251',reference))]
    ratios = [r['results']['mx251']['payloadBytes']/r['results'][reference]['payloadBytes'] for r in pairs]
    return dict(pairedCProjects=len(pairs), at110=sum(x<=1.1 for x in ratios), at120=sum(x<=1.2 for x in ratios),
        medianPercent=100*statistics.median(ratios) if ratios else None,
        totalMxBytes=sum(r['results']['mx251']['payloadBytes'] for r in pairs),
        totalReferenceBytes=sum(r['results'][reference]['payloadBytes'] for r in pairs),
        over120=[r['id'] for r,x in zip(pairs,ratios) if x>1.2])


def report(root, output):
    source = m.read(root/'inputs.json')
    rows=[]
    for p in source['projects']:
        results={}
        for s in ('mx251','keil','stc-soft','stc-mdu32'):
            path=root/'projects'/p['id']/s/'result.json'
            result=m.read(path) if path.exists() else dict(status='NOT_RUN')
            results[s]={k:v for k,v in result.items() if k in ('status','payloadBytes','spanBytes','holesBytes','error','reason','hex','receipt','memory','buildVer')}
            if 'error' in results[s]:
                results[s]['error']=re.sub(r' - SN: [^\r\n]+','',results[s]['error'])
            if path.exists():results[s]['record']=m.pin(path)
        row=dict(id=p['id'],name=p['name'],cSources=p['cSources'],libraries=p.get('libraries',[]),results=results)
        if all(results[s]['status']=='BUILT' for s in ('mx251','keil')):
            row.update(mxKeilPercent=results['mx251']['payloadBytes']*100/results['keil']['payloadBytes'],
                       mxKeilDelta=results['mx251']['payloadBytes']-results['keil']['payloadBytes'])
        rows.append(row)
    suites=tuple(rows[0]['results'])
    assert not any(r['results'][s]['status']=='NOT_RUN' for r in rows for s in suites), 'Matrix is still running'
    totals={s:{group:dict(collections.Counter(r['results'][s]['status'] for r in rows if group=='all' or bool(r['cSources'])==(group=='C')))
               for group in ('all','C','assembly')} for s in suites}
    comparisons={s:compare(rows,s) for s in suites if s!='mx251'}
    common=[r for r in rows if r['cSources'] and all(r['results'][s]['status']=='BUILT' for s in suites)]
    common_totals={s:sum(r['results'][s]['payloadBytes'] for r in common) for s in suites}
    data=dict(scope='STC32G12K128 experiment box V9.6',source=source['source'],projects=len(rows),
        cProjects=sum(bool(r['cSources']) for r in rows),assemblyProjects=sum(not r['cSources'] for r in rows),
        inputs=m.pin(root/'inputs.json'),core=m.read(Path(m.read(root/'config.json')['core'])/'core.json'),
        statusCounts=totals,comparisons=comparisons,commonCohort=dict(count=len(common),ids=[r['id'] for r in common],totalBytes=common_totals),rows=rows,
        diagnosis=m.read(root/'diagnosis.json') if (root/'diagnosis.json').exists() else None,
        mxFailures=m.read(root/'mx-failures.json'),verification=m.read(root/'verification.json'),
        toolVersions=dict(keil='C251 V5.60.0.0',stc=sorted({r['results'][s]['buildVer'] for r in rows for s in ('stc-soft','stc-mdu32') if r['results'][s].get('buildVer')})),
        hardwareExecuted=False,runtimeMeasured=False,sourceAlgorithmsChanged=False,
        equivalence='NOT_ESTABLISHED; successful linking and code-size measurement only')
    m.save(output.with_suffix('.json'),data)
    text=['# STC32G 实验箱三方编译对比（2026-11-14）','',
        f'范围：官方 STC32G12K128 V9.6 包，{len(rows)} 个工程，其中 {data["cProjects"]} 个 C 工程、{data["assemblyProjects"]} 个纯汇编工程。',
        f'来源：[官方实验箱页面]({source["source"]["page"]})；页面标注版本 {source["source"]["pageVersion"]}；下载包 SHA256 `{source["source"]["sha256"]}`。','',
        '使用已封装的 `mx251-core-20261114`，本轮不修改编译器核心或例程算法。所有工程和失败记录均计入清单。','',
        '## 口径','',
        '- MX251：公开 Build.ps1 入口，STC32G12K128、small、默认 EDATA、ordinary-wide、C251Compatibility。普通整数使用 AUTO；浮点工程选择 native-math，显式 MDU32 库工程选择 native-int-mdu32。',
        '- Keil5：本机 C251 V5.60.0.0，XSMALL、ROM(LARGE)、原工程 OPTIMIZE 等级/SPEED、INTR2；EDATA/HDATA 分配范围 0–0xFFF；保留原工程 REMOVEUNUSED 选项。扩展中断使用向量适配器，PARM4 等调用约定原样保留。',
        '- STC-IDE：本轮重新下载官方 worker，目标 STC32G12K128，24 位指针、GB2312、优化开启，分别运行 soft 和 mdu32。',
        '- 体积为最终 HEX 有效数据字节，包含常量和初始化数据；地址空洞另存。110%/120% 只在双方成功的 C 工程中统计。',
        '- 只做编译与链接测试，未测执行时间，也未证明三方固件功能等价。不能据此宣布最终工程验收通过。',
        '- C251 关键字、字符接口及数字向量做既有语法适配；8 个工程缺省引用的 STC32G.H 从同包 COMM 目录补入所有工具输入。纯汇编保留原 A251 语法并实际调用 MX251 汇编器。',
        '- USB OMF 不删除、不替换；MX251 尚无完整自动 ABI 接入。STC-IDE worker 不接收原 A251 单元和 USB OMF 库，对应工程列为不支持。MDU32 算术库由各编译器对应算术运行库替代，原始运算保留。','',
        '## 编译结果','',
        '| 工具 | C 工程成功 | 纯汇编成功 | 全部状态 |','|---|---:|---:|---|']
    for s in suites:
        counts=totals[s]
        text.append(f'| {s} | {counts["C"].get("BUILT",0)}/{data["cProjects"]} | {counts["assembly"].get("BUILT",0)}/{data["assemblyProjects"]} | '+', '.join(f'{k}: {v}' for k,v in counts['all'].items())+' |')
    text+=['','## 体积对比','', '| 参照 | 可比 C 工程 | MX ≤110% | MX ≤120% | 中位比值 | MX 总字节 / 参照总字节 |', '|---|---:|---:|---:|---:|---:|']
    for s,c in comparisons.items():
        text.append(f'| {s} | {c["pairedCProjects"]} | {c["at110"]} | {c["at120"]} | {c["medianPercent"]:.2f}% | {c["totalMxBytes"]} / {c["totalReferenceBytes"]} |')
    text+=['',f'三方均成功的同一组 {len(common)} 个 C 工程，按有效字节相加：', '',
        '| 工具 | 总字节 | 相对 Keil |','|---|---:|---:|']
    for s in suites:
        text.append(f'| {s} | {common_totals[s]} | {100*common_totals[s]/common_totals["keil"]:.2f}% |')
    text+=['','## 差距与后续顺序','',
        '1. 先补齐 STC32G 目标配置。当前仍固定生成 62 个向量；无中断的 g000 有效 HOME 区为 255 B，其中 248 B 来自未用向量桩。完整体积 307 B，而 Keil 为 60 B、STC-IDE 为 35 B。按需求生成向量是已定位的共性收益，尚未在本轮修改或计入收益。',
        '2. STC32G 配置未启用已有 nativeCodeReads 和 sph85-v1 能力；其有序外设位操作范围也较旧。官方 STC32G.H 的 SPH 为 0x85，而当前编译器元数据仍标记 legacy-v1（栈寄存器识别使用 0xBE）。应核对并补齐目标能力，不能把这些开关差距归因于优化算法本身。',
        '3. MX251 有 8 项自身构建失败：g079 报 unit 属性不存在；g102、g105–g110 共 7 项报 Missing EOF or reset vector。优先修复这两条路径。主表统一使用封装核心；输入适配和 Keil pragma 处理修正前的日志另行保留。',
        '4. g083 MDU32 原例程存在 printf 参数宽度不匹配，格式化器记录为 format-argument-width-mismatch，拉入完整 printf；9720 B 中 printf/vprintf/print_format 区段合计 6680 B。此工程需先处理输入格式问题，才能作有效的优化判断。',
        '5. 5 个 USB 二进制库工程仍需完整 OMF/ABI 接入；39 个纯 A251 工程是汇编语法接入工作，不能用 C 代码生成优化解决。',
        '6. 官方输入问题：g072 科学计算器在 Keil 上含未定义标识符/声明错误，直接编译原始 nor_cal.c 也复现 30 个错误，记录在 native-check-g072。g140 的 DOWNLOAD、SVCC_E、LED_POWER 未定义，三方均报错。',
        '7. STC-IDE 的 soft / mdu32 两次配置在本包成功的 79 项上生成相同 HEX；请求参数和官方 worker 版本均已记录。','',
        '## C 工程失败与不支持项','', '| 工具 | 编号 | 状态 | 日志/记录 |', '|---|---|---|---|']
    for r in rows:
        if not r['cSources']:continue
        for s in suites:
            x=r['results'][s]
            if x['status']!='BUILT':
                text.append(f'| {s} | {r["id"]} | {x["status"]} | [{s}/result.json]({(root/"projects"/r["id"]/s/"result.json").as_posix()}) |')
    text+=['','## 对 Keil 绝对增量最大的 C 工程','', '| 编号 | 工程 | MX | Keil | MX/Keil | 增量 |','|---|---|---:|---:|---:|---:|']
    for r in sorted((r for r in rows if r['cSources'] and 'mxKeilDelta' in r),key=lambda r:r['mxKeilDelta'],reverse=True)[:15]:
        text.append(f'| {r["id"]} | {r["name"]} | {r["results"]["mx251"]["payloadBytes"]} | {r["results"]["keil"]["payloadBytes"]} | {r["mxKeilPercent"]:.2f}% | {r["mxKeilDelta"]:+d} |')
    text+=['','## 全部工程','', '| 编号 | 工程 | MX251 | Keil5 | STC soft | STC mdu32 | MX/Keil |','|---|---|---:|---:|---:|---:|---:|']
    for r in rows:
        cells=[str(r['results'][s].get('payloadBytes',r['results'][s]['status'])) for s in suites]
        text.append('| '+r['id']+' | '+r['name']+' | '+' | '.join(cells)+' | '+(f'{r["mxKeilPercent"]:.2f}%' if 'mxKeilPercent' in r else '—')+' |')
    text+=['','完整诊断、HEX、MAP、构建参数和输入 SHA256 保存在：',f'`{root}`。','',
        '运行器为 `tools/research/stc32g_box_matrix.py`，报告器为 `tools/research/report_stc32g_box.py`。',
        '运行器参数：`--root <上述证据目录> --baseline E:/STVAPP/MX251-baseline --corpus E:/MCU/MX251-STC-SDK/research/official-box-comparison-20261101 --jobs 4`。',
        '默认复用已落盘结果；新一轮应使用新的证据目录。源码包、官方 worker、原始工程、适配文件及工具指纹均已保留。','']
    output.with_suffix('.md').write_text('\n'.join(text),encoding='utf8')
    print(totals)
    print(comparisons)


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path)
    args=ap.parse_args()
    report(args.root,args.output)
