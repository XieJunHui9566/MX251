"""Compare a new MX251 cohort with a frozen uniform build and its references."""
import argparse
import collections
import html
import json
from pathlib import Path

from unified_sdk_matrix import image, pin, read


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--title')
    p.add_argument('--exclude-ids', default='', help='Comma-separated IDs omitted from this comparison')
    args = p.parse_args()
    baseline = read(args.baseline)
    manifest = read(args.candidate / 'inputs.json')
    assert manifest['configuration']['mxSwitches'] == baseline['configuration']['mxSwitches']
    excluded = (set(filter(None, args.exclude_ids.split(',')))
                | set(manifest['configuration'].get('excludeIds', []))
                | set(baseline.get('excludedIds', [])))
    baseline_ids = {r['id'] for r in baseline['rows']}
    candidate_ids = {r['id'] for r in manifest['projects']}
    assert excluded <= baseline_ids | candidate_ids | set(baseline.get('excludedIds', [])), excluded
    assert baseline_ids - excluded == candidate_ids - excluded
    rows = []
    for old in baseline['rows']:
        if old['id'] in excluded:
            continue
        resultfile = args.candidate / 'projects' / old['id'] / 'mx251/result.json'
        result = read(resultfile)
        assert result['status'] == 'BUILT', (old['id'], result)
        actual, _ = image(result['hex']['path'])
        assert actual['hex']['sha256'] == result['hex']['sha256']
        assert actual['payloadBytes'] == result['payloadBytes']
        old_evidence = old['evidence'].get('candidate', old['evidence'].get('mx251'))
        old_result = read(Path(old_evidence['path']))
        old_receipt = read(Path(old_result['receipt']['path']))
        receipt = read(Path(result['receipt']['path']))
        assert receipt['compilerSha256'] == manifest['compiler']['sha256']
        inputs = lambda r: {k: v['sha256'] for k, v in r['inputs'].items()}
        assert inputs(old_receipt) == inputs(receipt), old['id']
        row = {k: old[k] for k in ('id', 'name', 'keilBytes', 'stcBytes', 'stcStatus', 'semanticNotes', 'originalSourceIssue')}
        row.update(parentBytes=old['mx251Bytes'], mx251Bytes=result['payloadBytes'],
                   savedBytes=old['mx251Bytes']-result['payloadBytes'], sourceInputsIdentical=True,
                   evidence=dict(parent=pin(Path(old_evidence['path'])), candidate=pin(resultfile),
                                 keil=old['evidence']['keil'], stc=old['evidence']['stc']))
        row['keilRatio'] = row['mx251Bytes'] / row['keilBytes']
        row['within110'] = 10*row['mx251Bytes'] <= 11*row['keilBytes']
        row['within120'] = 5*row['mx251Bytes'] <= 6*row['keilBytes']
        rows.append(row)
    assert rows and len(rows) == len(candidate_ids - excluded)
    title = args.title or f'MX251 编译器更新：{len(rows)} 项体积对比'
    summary = dict(projects=len(rows), improved=sum(r['savedBytes'] > 0 for r in rows),
                   unchanged=sum(r['savedBytes'] == 0 for r in rows), larger=sum(r['savedBytes'] < 0 for r in rows),
                   savedBytes=sum(r['savedBytes'] for r in rows), mxTotalBytes=sum(r['mx251Bytes'] for r in rows),
                   keilTotalBytes=sum(r['keilBytes'] for r in rows),
                   within110=sum(r['within110'] for r in rows), within120=sum(r['within120'] for r in rows),
                   stcStatuses=dict(collections.Counter(r['stcStatus'] for r in rows)),
                   smallerThanStcRaw=sum(r['stcBytes'] is not None and r['mx251Bytes'] < r['stcBytes'] for r in rows))
    summary['weightedKeilRatio'] = summary['mxTotalBytes'] / summary['keilTotalBytes']
    record = dict(status='COMPLETE_UNIFORM_COMPILER_DELTA', summary=summary, rows=rows,
                  excludedIds=sorted(excluded),
                  baseline=pin(args.baseline), candidateManifest=pin(args.candidate / 'inputs.json'),
                  compiler=manifest['compiler'], configuration=manifest['configuration'],
                  measurement='Unique Intel HEX payload bytes, excluding address gaps.',
                  referenceScope='Keil and STC measurements reused from the frozen baseline; source SHA-256 sets match.',
                  stcScope=baseline['stcScope'], fullProjectRuntimeMeasured=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix('.json').write_text(json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf8', newline='\n')
    intro = f"{len(rows)}/{len(rows)} 项构建结果可比；{summary['improved']} 项缩小、{summary['unchanged']} 项相同、{summary['larger']} 项变大，共减少 {summary['savedBytes']} B。"
    counts = f"体积 ≤Keil 120%：{summary['within120']} 项，其中 ≤110%：{summary['within110']} 项；合计 {summary['mxTotalBytes']}/{summary['keilTotalBytes']} B（{summary['weightedKeilRatio']:.2%}）。"
    scope = '全部工程采用同一编译器和配置。Keil/STC 复用上一轮相同源输入的结果；STC 仅作原始 HEX 数值参考。完整工程运行时间未测。'
    if excluded:
        scope += ' 本表排除：' + ', '.join(sorted(excluded)) + '；排除项目不计入分母或节省字节。'
    headings = ['ID', '工程', '父版/B', '本轮/B', '减少/B', 'Keil/B', 'MX/Keil', 'STC/B']
    lines = ['# '+title, '', intro, '', counts, '', scope, '', '| '+' | '.join(headings)+' |',
             '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    table = []
    for row in rows:
        cells = [row['id'], row['name'], row['parentBytes'], row['mx251Bytes'], row['savedBytes'], row['keilBytes'],
                 f"{row['keilRatio']:.2%}", row['stcBytes'] if row['stcBytes'] is not None else row['stcStatus']]
        lines.append('| '+' | '.join(str(v).replace('|', '\\|') for v in cells)+' |')
        table.append(f'<tr data-ratio="{row["keilRatio"]}" data-saved="{row["savedBytes"]}">'+
                     ''.join('<td>'+html.escape(str(v))+'</td>' for v in cells)+'</tr>')
    lines += ['', '编译器 SHA-256：`'+manifest['compiler']['sha256']+'`。逐项来源见同名 JSON。']
    args.output.with_suffix('.md').write_text('\n'.join(lines)+'\n', encoding='utf8', newline='\n')
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>TITLE</title>
<style>body{font:15px/1.7 system-ui,sans-serif;margin:32px;background:#f3f5f8;color:#203044}main{max-width:1500px;margin:auto}table{border-collapse:collapse;background:white;width:100%}th,td{padding:8px 12px;border-bottom:1px solid #dfe5ec;text-align:left}td:nth-child(n+3){text-align:right;white-space:nowrap}th{position:sticky;top:0;background:#203044;color:white}input,button{padding:10px;margin:10px 8px 18px 0}small{overflow-wrap:anywhere}</style>
<main><h1>TITLE</h1><p>INTRO</p><p>COUNTS</p><p>SCOPE</p>
<input id="q" placeholder="搜索 ID 或工程名称" size="42"><label><input id="changed" type="checkbox">只看有变化的工程</label><button id="sort">按 MX/Keil 降序</button><span id="count"></span>
<table><thead><tr>HEADINGS</tr></thead><tbody>ROWS</tbody></table><p><small>编译器 SHA-256：HASH</small></p></main>
<script>const q=document.querySelector('#q'),c=document.querySelector('#changed'),b=document.querySelector('tbody');function filter(){let n=0;for(const r of b.rows){const show=r.textContent.toLowerCase().includes(q.value.toLowerCase())&&(!c.checked||Number(r.dataset.saved)!==0);r.hidden=!show;if(show)n++}document.querySelector('#count').textContent=n+' 项'}q.oninput=c.onchange=filter;document.querySelector('#sort').onclick=()=>[...b.rows].sort((a,z)=>Number(z.dataset.ratio)-Number(a.dataset.ratio)).forEach(r=>b.append(r));filter();</script></html>'''
    for key, value in {'TITLE':html.escape(title), 'INTRO':intro, 'COUNTS':counts, 'SCOPE':scope,
                       'HEADINGS':''.join('<th>'+h+'</th>' for h in headings), 'ROWS':''.join(table), 'HASH':manifest['compiler']['sha256']}.items():
        page = page.replace(key, value)
    args.output.with_suffix('.html').write_text(page, encoding='utf8', newline='\n')
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == '__main__':
    main()
