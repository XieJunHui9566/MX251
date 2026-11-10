"""Publish all rows from a completed unified rebuild, without selecting minima."""
from pathlib import Path
import argparse
import collections
import html
import json
import math
import statistics

from unified_sdk_matrix import image, pin, read, save


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--references', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    manifest = read(args.candidate/'inputs.json')
    reference_manifest = read(args.references/'inputs.json')
    references = {r['id']: r for r in reference_manifest['projects']}
    rows, counters, startup, leaf = [], collections.Counter(), collections.Counter(), collections.Counter()
    for project in manifest['projects']:
        key = project['id']
        assert project['sourceManifest']['sha256'] == references[key]['sourceManifest']['sha256']
        row = dict(id=key, name=project['name'], evidence={}, semanticNotes=[])
        for suite, root in [('mx251', args.candidate), ('keil', args.references), ('stc', args.references)]:
            path = root/'projects'/key/suite/'result.json'
            result = read(path)
            counters[(suite, result['status'])] += 1
            row[suite+'Status'] = result['status']
            row[suite+'Bytes'] = result.get('payloadBytes')
            row[suite+'BuildSeconds'] = result['wallSeconds']
            row['evidence'][suite] = pin(path)
            if result['status'] == 'BUILT':
                actual, _ = image(result['hex']['path'])
                assert actual['hex']['sha256'] == result['hex']['sha256']
                assert actual['payloadBytes'] == result['payloadBytes']
            if suite == 'stc':
                diagnostics = result.get('diagnostics', {})
                text = diagnostics.get('notes', []) + diagnostics.get('warnings', [])
                row['semanticNotes'] = [s for s in text if any(fragment in s for fragment in ('没有找到定义', '剥离了', '13 字节值'))]
                row['stcError'] = result.get('error', result.get('reason'))
            elif suite == 'mx251' and result['status'] == 'BUILT':
                receipt = read(result['receipt']['path'])
                startup[receipt['startupRuntime']['status']] += 1
                leaf[receipt['selectiveLeafAbi']['status']] += 1
        row['keilRatio'] = row['mx251Bytes']/row['keilBytes'] if row['mx251Bytes'] and row['keilBytes'] else None
        row['within110'] = row['keilRatio'] is not None and 10*row['mx251Bytes'] <= 11*row['keilBytes']
        row['within120'] = row['keilRatio'] is not None and 5*row['mx251Bytes'] <= 6*row['keilBytes']
        row['mxStcRatio'] = row['mx251Bytes']/row['stcBytes'] if row['mx251Bytes'] and row['stcBytes'] else None
        rows.append(row)
    assert len(rows) == 75
    comparable = [r for r in rows if r['keilRatio'] is not None]
    stc = [r for r in rows if r['mxStcRatio'] is not None]
    summary = dict(projects=len(rows), builds={s: {status: n for (suite, status), n in counters.items() if suite == s}
                    for s in ('mx251', 'keil', 'stc')}, keilComparable=len(comparable),
                   within110=sum(r['within110'] for r in rows), within120=sum(r['within120'] for r in rows),
                   mxTotalBytes=sum(r['mx251Bytes'] for r in comparable), keilTotalBytes=sum(r['keilBytes'] for r in comparable),
                   medianKeilRatio=statistics.median(r['keilRatio'] for r in comparable),
                   geometricMeanKeilRatio=math.exp(statistics.mean(math.log(r['keilRatio']) for r in comparable)),
                   smallerThanKeil=sum(r['mx251Bytes'] < r['keilBytes'] for r in comparable),
                   stcRawComparable=len(stc), smallerThanStcRaw=sum(r['mx251Bytes'] < r['stcBytes'] for r in stc),
                   mxStcSubsetBytes=sum(r['mx251Bytes'] for r in stc), stcSubsetBytes=sum(r['stcBytes'] for r in stc),
                   stcSemanticDifferenceDiagnostics=sum(bool(r['semanticNotes']) for r in stc),
                   startupStatuses=dict(startup), leafStatuses=dict(leaf), fullProjectRuntimeQualified=0)
    summary['weightedKeilRatio'] = summary['mxTotalBytes']/summary['keilTotalBytes']
    summary['weightedStcRawRatio'] = summary['mxStcSubsetBytes']/summary['stcSubsetBytes']
    record = dict(status='COMPLETE_OFFLINE_MATRIX', summary=summary, rows=rows,
                  configuration=manifest['configuration'], compiler=manifest['compiler'],
                  candidateManifest=pin(args.candidate/'inputs.json'), referenceManifest=pin(args.references/'inputs.json'),
                  measurement='Unique Intel HEX payload bytes; address gaps and HEX text overhead excluded.',
                  runtime='Firmware execution time was not measured. Build seconds are host wall time under concurrent load.',
                  stcScope='Current official worker; native 24-bit-pointer configuration, not Keil XSMALL equivalence. Raw size comparison only.',
                  hardwareExecuted=False)
    save(args.output.with_suffix('.json'), record)
    lines = ['# MX251 统一版本：75 工程完整对比', '',
             f"编译器 SHA-256：`{manifest['compiler']['sha256']}`。统一使用最新后端、函数共享、调用保存、原生整数库、自动叶函数 ABI、启动裁剪及 C251 bit 兼容。", '',
             f"MX251 编译成功 **{counters[('mx251', 'BUILT')]}/75**；Keil 成功 **{counters[('keil', 'BUILT')]}/75**；STC-IDE 生成 HEX **{counters[('stc', 'BUILT')]}/75**。", '',
             f"相对 Keil，体积 ≤110%：**{summary['within110']}**；≤120%：**{summary['within120']}**（包含前者）；超过 120%：**{len(comparable)-summary['within120']}**。", '',
             f"75 项体积合计 MX251/Keil：**{summary['mxTotalBytes']} / {summary['keilTotalBytes']} B**，加权比 **{summary['weightedKeilRatio']:.2%}**，逐工程比值中位数 **{summary['medianKeilRatio']:.2%}**。", '',
             f"STC-IDE 有 {len(stc)} 项原始 HEX 数值可比较，MX251 在其中 {summary['smallerThanStcRaw']} 项更小；其中 {summary['stcSemanticDifferenceDiagnostics']} 项 STC 诊断提及 far 剥离、未定义外部变量另行分配等语义差异。STC 数值只作独立工具链参考，不计入同模型达线数量。", '',
             '体积按 HEX 实际地址上的有效字节计数，排除空洞和文本开销。运行时间本次未测；完整工程双体积/时间达标记录仍为 0。所有工程保持既有算法，只沿用方言、初始化括号和中断向量适配。', '',
             '## 全部工程', '', '| ID | 工程 | MX251/B | Keil/B | MX/Keil | STC-IDE/B | 备注 |',
             '| --- | --- | ---: | ---: | ---: | ---: | --- |']
    table = []
    for row in rows:
        ratio = f"{row['keilRatio']:.2%}" if row['keilRatio'] is not None else '—'
        note = '≤110%' if row['within110'] else '≤120%' if row['within120'] else '>120%'
        if row['stcStatus'] == 'UNSUPPORTED_ASSEMBLY_INPUT':
            note += '；STC 汇编输入不支持'
        elif row['stcStatus'] == 'FAILED':
            note += '；STC 编译失败'
        elif row['semanticNotes']:
            note += '；STC 语义差异诊断'
        values = [row['id'], row['name'], row['mx251Bytes'] or '—', row['keilBytes'] or '—', ratio, row['stcBytes'] or '—', note]
        lines.append('| '+' | '.join(str(v).replace('|', '\\|') for v in values)+' |')
        cells = ''.join('<td>'+html.escape(str(v))+'</td>' for v in values)
        table.append(f'<tr data-ratio="{row["keilRatio"] or 999}" data-tier="{110 if row["within110"] else 120 if row["within120"] else 999}">{cells}</tr>')
    args.output.with_suffix('.md').write_text('\n'.join(lines)+'\n', encoding='utf-8', newline='\n')
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>MX251 · 75 工程对比</title>
<style>body{font:15px/1.6 system-ui,sans-serif;background:#f5f7fa;color:#182333;margin:32px}main{max-width:1550px;margin:auto}h1{font-size:30px}.cards{display:flex;gap:16px;margin:24px 0}.card{background:white;padding:18px 26px;border-radius:10px;flex:1}.big{font-size:32px;font-weight:650}input,select,button{padding:10px;border:1px solid #cbd4df;border-radius:6px;margin:8px 8px 16px 0}table{border-collapse:collapse;width:100%;background:white}th,td{padding:9px 12px;border-bottom:1px solid #e4e9ef;text-align:left}th{position:sticky;top:0;background:#152b44;color:white}td:nth-child(n+3):nth-child(-n+6){text-align:right;white-space:nowrap;font-variant-numeric:tabular-nums}tr[data-tier="110"] td:first-child{border-left:4px solid #168763}tr[data-tier="120"] td:first-child{border-left:4px solid #c49c31}.muted{color:#586779}small{overflow-wrap:anywhere}</style>
<main><h1>MX251 · 75 工程统一版本对比</h1><p class="muted">2026-11-10 · 同一个编译器、同一套配置、完整重编 · 字节按 HEX 有效载荷统计</p>
<div class="cards"><div class="card">MX251 构建成功<div class="big">BUILD / 75</div></div><div class="card">体积 ≤110% Keil<div class="big">PASS110 / 75</div></div><div class="card">体积 ≤120% Keil<div class="big">PASS120 / 75</div></div><div class="card">总体积 / Keil<div class="big">RATIO</div></div></div>
<p>STC-IDE：STCBUILD 项生成 HEX；13 项的原始汇编输入不受官方 worker 支持，其余失败项见表格。STC 使用自身 24 位指针配置，带语义差异诊断的结果仅供数值参考。完整工程运行时间未测。</p>
<input id="query" placeholder="搜索 ID 或工程名称" size="44"><select id="tier"><option value="all">全部 75 项</option><option value="110">≤110%</option><option value="120">110%～120%</option><option value="999">>120%</option></select><button id="sort">按 MX/Keil 从大到小排序</button><span id="count"></span>
<table><thead><tr><th>ID</th><th>工程</th><th>MX251/B</th><th>Keil/B</th><th>MX/Keil</th><th>STC-IDE/B</th><th>备注</th></tr></thead><tbody>ROWS</tbody></table>
<p class="muted"><small>编译器 SHA-256：HASH。详细来源、配置和诊断保存在同名 JSON 中。</small></p></main>
<script>const q=document.querySelector('#query'),t=document.querySelector('#tier'),body=document.querySelector('tbody');function filter(){let n=0;for(const row of body.rows){const show=row.textContent.toLowerCase().includes(q.value.toLowerCase())&&(t.value==='all'||row.dataset.tier===t.value);row.hidden=!show;if(show)n++}document.querySelector('#count').textContent=n+' 项'}q.oninput=filter;t.onchange=filter;document.querySelector('#sort').onclick=()=>{[...body.rows].sort((a,b)=>Number(b.dataset.ratio)-Number(a.dataset.ratio)).forEach(r=>body.append(r))};filter();</script></html>'''
    for key, value in {'STCBUILD': counters[('stc', 'BUILT')], 'BUILD': counters[('mx251', 'BUILT')], 'PASS110': summary['within110'],
                       'PASS120': summary['within120'], 'RATIO': f"{summary['weightedKeilRatio']:.2%}",
                       'ROWS': ''.join(table), 'HASH': manifest['compiler']['sha256']}.items():
        page = page.replace(key, str(value))
    args.output.with_suffix('.html').write_text(page, encoding='utf-8', newline='\n')
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == '__main__':
    main()
