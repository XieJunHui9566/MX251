"""Combine affected public builds with byte-identical compile evidence."""
import argparse
from collections import Counter
from pathlib import Path
import re
import statistics
from unified_sdk_matrix import read, save, pin
from vector_gc_attribution import image_parts
from call_layout import ihex, read_unit
from isr_object_report import bodies, transfer_widths


def compare(before, after, commit):
    previous = read(before/'comparison.json')
    census = read(after/'census/result.json')
    compiler = read(after/'candidate/core/core.json')['compiler']['sha256']
    assert compiler == census['compilerSha256']
    affected = set(census['affectedProjects'])
    rows, changes, body_changes, resolved = [], [], [], []
    for old in previous['rows']:
        key = old['id']
        row = dict(id=key, name=old['name'], status=old['status'], before=old['new'],
                   new=old['new'], keil=old['keil'], stc=old['stc'])
        rows.append(row)
        prior = before/'projects'/key/'mx251'
        if key not in affected:
            row['verification'] = 'object-identical' if old['status']=='BUILT' else 'inherited-status'
            row['result'] = pin(prior/'result.json')
            if old['status']=='BUILT':
                modules = [r for r in census['rows'] if r['id']==key]
                assert modules and all(not r['changed'] for r in modules), key
            continue
        current = after/'projects'/key/'mx251'
        inputs = [Path(n) for n in ('mx251.json', 'CMakeLists.txt', 'dialect.json')]
        inputs += [f.relative_to(prior) for f in (prior/'src').rglob('*') if f.is_file()]
        assert all((prior/f).read_bytes()==(current/f).read_bytes() for f in inputs), key
        result = read(current/'result.json')
        assert result['status']=='BUILT', key
        row.update(new=result['payloadBytes'], verification='public-rebuild', result=pin(current/'result.json'))
        latest, original = current/'.build/mx251/latest', prior/'.build/mx251/latest'
        assert read(latest/'receipt.json')['compilerSha256']==compiler
        old_bodies, old_notes = bodies(original)
        new_bodies, notes = bodies(latest)
        for symbol, body in new_bodies.items():
            if any('reason=body-effect instruction=' in n and re.search(r'instruction=(movc|div|mul)\b', n)
                   for n in old_notes.get(symbol, [])):
                assert any('mx251-leaf-isr-saves ' in n for n in notes[symbol]), (key, symbol)
                resolved.append(dict(id=key, symbol=symbol, diagnostics=notes[symbol]))
            old_code, new_code = transfer_widths(old_bodies[symbol]), transfer_widths(body)
            if old_code != new_code:
                at, extra = 0, []
                for line in new_code:
                    if at<len(old_code) and line==old_code[at]: at+=1
                    else: extra.append(line)
                assert at==len(old_code) and extra and all(re.fullmatch(r'(push|pop)\s+r(8|9|1[2-5])', s) for s in extra), (key, symbol, extra)
                assert sorted(s.split()[1] for s in extra if s.startswith('push'))==sorted(s.split()[1] for s in extra if s.startswith('pop'))
                body_changes.append(dict(id=key, symbol=symbol, kind='division-context-save-fix', added=extra))
        old_functions = {f['name']: f for f in image_parts(original)['functions']}
        for f in image_parts(latest)['functions']:
            old_f = old_functions.get(f['name'])
            if not old_f or old_f['bytes']==f['bytes']: continue
            change = dict(id=key, symbol=f['name'], isr=f['isr'], before=old_f['bytes'], new=f['bytes'],
                          saveRestoreBefore=old_f['entryExitPushPopBytes'], saveRestoreAfter=f['entryExitPushPopBytes'])
            if not f['isr']:
                def instructions(folder, item):
                    unit = read_unit(folder, item['unit'], ihex(folder/'firmware.hex'))
                    return [i.source for i in unit['instructions'] if i.owner==item['name']]
                assert transfer_widths(instructions(original,old_f))==transfer_widths(instructions(latest,f)), change
                change['layoutOnly'] = True
            changes.append(change)
    built = [r for r in rows if r['status']=='BUILT']
    for r in built: r['savedBytes'] = r['before']-r['new']
    def reference(name):
        chosen = [r for r in built if r[name] is not None]
        return dict(count=len(chosen), mxBytes=sum(r['new'] for r in chosen), referenceBytes=sum(r[name] for r in chosen),
                    at110=sum(r['new']*100<=r[name]*110 for r in chosen),
                    at120=sum(r['new']*100<=r[name]*120 for r in chosen),
                    new110=[r['id'] for r in chosen if r['new']*100<=r[name]*110<r['before']*100],
                    new120=[r['id'] for r in chosen if r['new']*100<=r[name]*120<r['before']*100],
                    medianPercent=statistics.median(100*r['new']/r[name] for r in chosen))
    newly_resolved = {(r['id'],r['symbol']) for r in resolved}
    remaining = [r for r in previous['remaining'] if (r['id'],r['symbol']) not in newly_resolved]
    hardware = read(after/'hardware-capture/result.json')
    return dict(schema=1, implementationCommit=commit, compilerSha256=compiler,
                parentReport=pin(before/'comparison.json'), census=pin(after/'census/result.json'),
                sourceAlgorithmsChanged=False, rows=rows, resolved=resolved, remaining=remaining,
                changedFunctions=changes, changedIsrBodies=body_changes,
                summary=dict(projects=len(rows), statuses=dict(Counter(r['status'] for r in rows)),
                    replayedProjects=census['projects'], replayedUnits=census['units'], fullRebuilds=len(affected),
                    beforeBytes=sum(r['before'] for r in built), afterBytes=sum(r['new'] for r in built),
                    reduced=sum(r['savedBytes']>0 for r in built), increased=sum(r['savedBytes']<0 for r in built),
                    originalObjectCohortResolved=len(previous['resolved'])+len(previous['remaining'])-len(remaining),
                    keil=reference('keil'), stc=reference('stc')),
                compilerTests=dict(count=len(read(after/'verified-tests/results.json')), evidence=pin(after/'verified-tests/results.json')),
                hardware=dict(status=hardware['status'], cases=hardware['cases'], frames=hardware['frames'],
                    firmwareSha256=hardware['firmwareSha256'], evidence=pin(after/'hardware-capture/result.json'),
                    receipt=pin(after/'hardware-project/.build/mx251/latest/receipt.json'),
                    source=pin(after/'hardware-project/source.json')),
                timing='Only arithmetic and context correctness checked; full engineering runtime ratios not measured.')


def markdown(result, after):
    s = result['summary']; k = s['keil']; stc = s['stc']
    text = ['# CODE 读取和原生乘除法的中断保存分析', '',
        f"实现提交：`{result['implementationCommit']}`；编译器 SHA-256：`{result['compilerSha256']}`。", '',
        '通用 ISR 分析接受 MOVC A,@A+DPTR / @A+PC，并复用已有 MUL/DIV 寄存器描述，包含隐式输出的高半寄存器。没有增加器件开关，也没有修改 SDK 算法。', '',
        '同时修正旧后端原生 16 位除法的上下文保存：ISR/callee-saves 入口不能仅按当前函数的值活跃性删除临时寄存器保存。乘除法现在共用这一保存判据；优化关闭时，此正确性修复仍生效。', '',
        f"对全部 {s['replayedProjects']} 个成功工程的 {s['replayedUnits']} 个 C 编译单元，使用相同的最终反馈分别调用父版与新版。只有 3 个工程的 REL 改变；这 3 项通过公开入口重新完整构建。其余 94 项沿用目标文件逐字节相同的父版链接结果，Keil/STC-IDE 分母也沿用冻结参考。没有把继承结果记作新版完整重编。", '',
        '| 工程 | 父版 B | 本轮 B | 减少 B | 相对 Keil | 相对 STC-IDE |',
        '| --- | ---: | ---: | ---: | ---: | ---: |']
    for r in result['rows']:
        if r.get('savedBytes'):
            text.append(f"| {r['id']} {r['name']} | {r['before']} | {r['new']} | {r['savedBytes']} | {100*r['new']/r['keil']:.2f}% | {100*r['new']/r['stc']:.2f}% |")
    text += ['', f"97 项合计 {s['beforeBytes']} → {s['afterBytes']} B，减少 {s['beforeBytes']-s['afterBytes']} B；3 项缩小、94 项相同、0 项增大。", '',
        f"Keil 体积线：≤110% 为 {k['at110']}/97，≤120% 为 {k['at120']}/97；g135 新进入 120% 线。STC-IDE 可比 79 项中，≤110% 为 {stc['at110']}/79，≤120% 为 {stc['at120']}/79。5 项 OMF 未支持、1 项官方源码错误和39项纯汇编状态沿用上一轮，本次未重跑。", '',
        '原间接写入阻塞的 57 个 ISR 现为 57/57 完成保存精简。本轮实际解开4个 ISR：g079 PWMA、g133 PWMB/Timer0、g135 PWMB。它们不代表所有 ISR 都已优化，未知调用效果仍是其他中断的阻塞原因。', '',
        'g133 Timer0 的入口/出口保存从60降至20 B，函数体新增24 B临时寄存器保存以修复上下文，净减16 B。另有3个普通函数仅因既有页面调用/跳转布局改变宽度，工程合计额外减少2 B；没有改变其计算指令。', '',
        f"编译/汇编检查通过 {result['compilerTests']['count']} 组：small/large × speed/size，涵盖 CODE 字节/字读取、8/16位商余数、16×16乘法、原有边界和优化关闭。", '',
        f"AI8051U 实板通过 {result['hardware']['cases']} 组、{result['hardware']['frames']} 帧；Timer0/1 在4种寄存器银行上下文中抢占，检查 R0～R31、DPX、PSW/PSW1、SPX，以及独立 Python 生成期望值的 CODE 查表、8/16位商余数和乘积。固件保留在板上。未测完整工程运行时间，不能据此宣称双110%达标。", '',
        f"开发入口：`{after/'Build-Optimized.ps1'}`。冻结发行基线未替换。", '',
        '后续优先调查剩余调用效果闭包：当前阻塞主要落在 DisplayScan、USB 请求处理等调用链，先衡量跨工程收益，再扩大实现。', '']
    return '\n'.join(text)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before', type=Path, required=True); p.add_argument('--after', type=Path, required=True)
    p.add_argument('--commit', required=True)
    a = p.parse_args(); result = compare(a.before, a.after, a.commit)
    save(a.after/'comparison.json', result)
    (a.after/'comparison.md').write_text(markdown(result,a.after), encoding='utf8', newline='\n')
    print(result['summary'])
