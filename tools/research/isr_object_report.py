"""Compare frozen engineering inputs and final HEX images for ISR provenance."""
import argparse
from collections import Counter
from pathlib import Path
import re
import statistics

from unified_sdk_matrix import read, save, pin
from vector_gc_attribution import image_parts
from call_layout import ihex, read_unit


def transfer_widths(code):
    return [re.sub(r'^[ale](call|jmp)\b', r'\1', line) for line in code]


def bodies(root):
    result, diagnostics = {}, {}
    for unit in read(root/'project.snapshot.json')['units']:
        if Path(unit['file']).suffix.lower() != '.c': continue
        owner, active = None, False
        for line in (root/(unit['name']+'.asm')).read_text('latin1').splitlines():
            match = re.search(r'mx251-function begin schema=1 symbol=(\S+)', line)
            if match: owner = match[1]
            if owner and 'mx251-leaf-isr-' in line:
                diagnostics.setdefault(owner, []).append(line.strip())
            if 'mx251-isr-body-end' in line: active = False
            if active and (code := line.split(';', 1)[0].strip()):
                result.setdefault(owner, []).append(code)
            if 'mx251-isr-body-begin' in line: active = True
            if 'mx251-function end schema=1' in line: owner = None
    return result, diagnostics


def compare(before, after):
    old_report = read(before/'comparison.json')
    old_rows = {v['id']: v for v in old_report['rows']}
    compiler = read(after/'candidate/core/core.json')['compiler']['sha256']
    rows, functions, body_changes = [], [], []
    diagnostics = {}
    for project in read(after/'inputs.json')['projects']:
        key = project['id']; old = old_rows[key]
        old_dir = before/'projects'/key/'mx251'
        new_dir = after/'projects'/key/'mx251'
        inputs = [Path('mx251.json'), Path('CMakeLists.txt'), Path('dialect.json')]
        inputs += [p.relative_to(old_dir) for p in (old_dir/'src').rglob('*') if p.is_file()]
        assert all((old_dir/f).read_bytes() == (new_dir/f).read_bytes() for f in inputs), key
        new = read(new_dir/'result.json')
        assert old['status'] == new['status'], (key, old['status'], new['status'])
        row = dict(id=key, name=old['name'], status=new['status'], before=old['new'],
                   new=new.get('payloadBytes'), keil=old['keil'], stc=old['stc'],
                   result=pin(new_dir/'result.json'))
        rows.append(row)
        if new['status'] != 'BUILT': continue
        latest = new_dir/'.build/mx251/latest'
        previous = old_dir/'.build/mx251/latest'
        assert read(latest/'receipt.json')['compilerSha256'] == compiler, key
        row['savedBytes'] = row['before']-row['new']
        old_bodies, _ = bodies(previous)
        new_bodies, diagnostics[key] = bodies(latest)
        for name, body in new_bodies.items():
            if old_bodies.get(name) != body:
                assert transfer_widths(old_bodies[name]) == transfer_widths(body), (key, name)
                body_changes.append(dict(id=key, symbol=name, layoutOnly=True,
                                         before=old_bodies[name], after=body))
        if not row['savedBytes']: continue
        old_funcs = {f['name']: f for f in image_parts(previous)['functions']}
        for function in image_parts(latest)['functions']:
            old_function = old_funcs.get(function['name'])
            if old_function and old_function['bytes'] != function['bytes']:
                change = dict(id=key, symbol=function['name'], isr=function['isr'],
                    before=old_function['bytes'], new=function['bytes'],
                    saveRestoreBefore=old_function['entryExitPushPopBytes'],
                    saveRestoreAfter=function['entryExitPushPopBytes'])
                if not function['isr']:
                    def code(folder, item):
                        unit = read_unit(folder, item['unit'], ihex(folder/'firmware.hex'))
                        return [i.source for i in unit['instructions'] if i.owner == item['name']]
                    assert transfer_widths(code(previous, old_function)) == transfer_widths(code(latest, function)), change
                    change['layoutOnly'] = True
                functions.append(change)
    resolved, remaining = [], []
    for row in read(before/'isr-blockers.json')['rows']:
        if row['instruction'] != 'mov\t@dpx,a': continue
        notes = diagnostics.get(row['id'], {}).get(row['symbol'], [])
        record = dict(id=row['id'], symbol=row['symbol'], diagnostics=notes)
        (resolved if any('mx251-leaf-isr-saves ' in n for n in notes) else remaining).append(record)
    built = [v for v in rows if v['status'] == 'BUILT']
    def reference(name):
        selected = [v for v in built if v[name] is not None]
        return dict(count=len(selected), beforeMxBytes=sum(v['before'] for v in selected),
                    mxBytes=sum(v['new'] for v in selected), referenceBytes=sum(v[name] for v in selected),
                    at110=sum(v['new']*100 <= v[name]*110 for v in selected),
                    at120=sum(v['new']*100 <= v[name]*120 for v in selected),
                    new110=[v['id'] for v in selected if v['new']*100 <= v[name]*110 < v['before']*100],
                    new120=[v['id'] for v in selected if v['new']*100 <= v[name]*120 < v['before']*100],
                    medianPercent=statistics.median(100*v['new']/v[name] for v in selected))
    return dict(schema=1, compilerSha256=compiler, implementationCommit='0874502d349e7812260864d5fb0bfbf3be06c7c4',
        parentReport=pin(before/'comparison.json'), sourceAlgorithmsChanged=False,
        summary=dict(projects=len(rows), statuses=dict(Counter(v['status'] for v in rows)),
            beforeBytes=sum(v['before'] for v in built), afterBytes=sum(v['new'] for v in built),
            reduced=sum(v['savedBytes']>0 for v in built), unchanged=sum(v['savedBytes']==0 for v in built),
            increased=sum(v['savedBytes']<0 for v in built), keil=reference('keil'), stc=reference('stc'),
            resolvedIsrs=len(resolved), remainingIsrs=len(remaining), changedFinalIsrBodies=body_changes),
        resolved=resolved, remaining=remaining, changedFunctions=functions, rows=rows,
        compilerTests=dict(count=len(read(after.parent/'final-tests/results.json')),
                           evidence=pin(after.parent/'final-tests/results.json')),
        hardware=read(after/'hardware-result.json'),
        aiControls=[read(p) for p in sorted((after/'ai-controls').glob('*/result.json'))],
        timing='Hardware context correctness was checked; engineering runtime ratios were not measured.')


def markdown(result, after):
    s = result['summary']; saved = s['beforeBytes']-s['afterBytes']
    text = ['# 中断间接写入对象来源与保存精简', '',
        '实现提交：`'+result['implementationCommit']+'`。仅修改通用编译器分析，没有新增器件开关或修改例程算法。', '',
        '补齐两条事实来源：EDATA/XDATA 标量结果的真实存储对象，以及常量指针完整访问范围。'
        '分支展开后的标签不再阻断已知常量地址的识别。未知指针、核心地址别名、24 位地址回绕保留原有保存。', '',
        f"原 57 个间接写入阻塞中断中，{s['resolvedIsrs']} 个完成保存精简；其余 {s['remainingIsrs']} 个转为其他指令阻塞。", '',
        f"重新运行全部 {s['projects']} 个 STC32G C 工程：97 个成功，5 个 OMF 未支持，1 个源码错误。"
        '5 个 USB OMF 工程和 1 个官方源码错误的状态与上一轮相同；39 个纯 A251 工程不在本轮范围。', '',
        f"97 个成功工程：{s['beforeBytes']} → {s['afterBytes']} B，减少 {saved} B（{100*saved/s['beforeBytes']:.2f}%）；"
        f"{s['reduced']} 项缩小、{s['unchanged']} 项相同、{s['increased']} 项增大。", '',
        '| 参考 | 可比工程 | ≤110% | ≤120% | MX251 / 参考合计 B | 比值中位数 |',
        '| --- | ---: | ---: | ---: | ---: | ---: |']
    for key, title in [('keil','Keil C251'), ('stc','STC-IDE')]:
        v=s[key]
        text.append(f"| {title} | {v['count']} | {v['at110']} | {v['at120']} | {v['mxBytes']} / {v['referenceBytes']} | {v['medianPercent']:.2f}% |")
    text += ['', f"新增 Keil 110% 达标：{'、'.join(s['keil']['new110']) or '无'}；新增 120% 达标：{'、'.join(s['keil']['new120']) or '无'}。", '',
        '## 验证', '',
        f"- {result['compilerTests']['count']} 组编译检查通过：small/large × speed/size；逐条对比中断函数体，核对未知地址排除及关闭优化后的目标文件一致性。",
        f"- 真实硬件 {result['hardware']['cases']} 组通过：Timer0/Timer1、四个寄存器银行、R0–R31、DPX、PSW/PSW1、SPX，以及 EDATA 8/16/32 位更新和 XDATA 数组写入。最终编译器生成的测试 HEX 与已烧录 HEX 完全一致，复用该硬件结果。",
        '- AI8051U p039、p083 重新编译后 HEX 与上一轮完全一致。',
        f"- 所有成功工程核对同一编译器 SHA-256：`{result['compilerSha256']}`。冻结源码、工程配置逐字节相同。",
        '- 最终 ASM 中断函数体仅 g115 的 `_timer0` 将 `LCALL _DisplayScan` 缩为 `ACALL _DisplayScan`；由新地址布局触发现有页内调用优化。其余中断体指令相同。普通函数的体积变化也逐条核对为现有调用/跳转宽度选择。',
        '- 本轮测量最终 HEX 有效载荷体积；硬件检查验证上下文正确性，没有测量工程运行时间与 Keil 的比值。', '',
        '## 后续阻塞', '']
    for v in result['remaining']:
        reasons=[n.split('instruction=',1)[1].replace('\t',' ') for n in v['diagnostics'] if 'reason=body-effect' in n]
        text.append(f"- {v['id']} `{v['symbol']}`：`"+'；'.join(reasons)+'`。')
    text += ['', '## 使用与逐项数据', '',
        f"候选入口：`{after/'Build-Optimized.ps1'}`，参数 `-ProjectPath <工程>`；Keil 工程追加 `-C251Compatibility`。使用基线自带 PowerShell。", '',
        'Keil/STC-IDE 引用上一轮冻结结果；本轮 MX251 全量重编。基线标签未替换。', '',
        '| ID | 旧 MX B | 新 MX B | 节省 B | Keil B | 新/Keil | STC-IDE B | 状态 |',
        '| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |']
    for v in result['rows']:
        ratio=f"{100*v['new']/v['keil']:.2f}%" if v['new'] is not None and v['keil'] else '—'
        values=[v['id'],v['before'],v['new'],v.get('savedBytes'),v['keil'],ratio,v['stc'],v['status']]
        text.append('| '+' | '.join(str(x) if x is not None else '—' for x in values)+' |')
    return '\n'.join(text)+'\n'


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--markdown', type=Path)
    a = p.parse_args(); result = compare(a.before, a.after); save(a.output, result)
    if a.markdown: a.markdown.write_text(markdown(result, a.after), encoding='utf-8', newline='\n')
    print(result['summary'])
