"""Measure full public builds against the mixed, pinned parent matrix."""
import argparse
from collections import Counter
from pathlib import Path
import re
import statistics
from unified_sdk_matrix import read, save, pin
from vector_gc_attribution import image_parts
from isr_object_report import bodies, transfer_widths


def compare(before, after):
    previous = read(before/'comparison.json')
    core = read(after/'candidate/core/core.json')
    compiler = core['compiler']['sha256']
    rows, functions, resolved, remaining, body_changes = [], [], [], [], []
    for old in previous['rows']:
        key = old['id']
        row = dict(id=key, name=old['name'], status=old['status'], before=old['new'],
                   new=old['new'], keil=old['keil'], stc=old['stc'])
        rows.append(row)
        old_result = Path(old['result']['path'])
        assert pin(old_result)['sha256'] == old['result']['sha256'], key
        if old['status'] != 'BUILT':
            row.update(verification='inherited-status', result=pin(old_result))
            continue
        prior = old_result.parent
        current = after/'projects'/key/'mx251'
        inputs = [Path(n) for n in ('mx251.json', 'CMakeLists.txt', 'dialect.json')]
        inputs += [f.relative_to(prior) for f in (prior/'src').rglob('*') if f.is_file()]
        assert all((prior/f).read_bytes() == (current/f).read_bytes() for f in inputs), key
        result = read(current/'result.json')
        assert result['status'] == 'BUILT', (key, result['status'])
        row.update(new=result['payloadBytes'], verification='public-rebuild',
                   result=pin(current/'result.json'), savedBytes=old['new']-result['payloadBytes'])
        original, latest = prior/'.build/mx251/latest', current/'.build/mx251/latest'
        assert read(latest/'receipt.json')['compilerSha256'] == compiler, key
        old_bodies, old_notes = bodies(original)
        new_bodies, notes = bodies(latest)
        for symbol, diagnostics in old_notes.items():
            if not any(re.search(r'reason=body-effect instruction=(?:e|l|a)call\s', n) for n in diagnostics):
                continue
            new_notes = notes.get(symbol, [])
            record = dict(id=key, symbol=symbol, before=diagnostics, after=new_notes)
            (resolved if any('mx251-leaf-isr-saves ' in n for n in new_notes) else remaining).append(record)
        for symbol, body in new_bodies.items():
            if transfer_widths(old_bodies[symbol]) != transfer_widths(body):
                body_changes.append(dict(id=key, symbol=symbol,
                                         before=old_bodies[symbol], after=body))
        if row['savedBytes']:
            old_functions = {(f['unit'], f['name']): f for f in image_parts(original)['functions']}
            for f in image_parts(latest)['functions']:
                prior_f = old_functions.get((f['unit'], f['name']))
                if prior_f and prior_f['bytes'] != f['bytes']:
                    functions.append(dict(id=key, symbol=f['name'], unit=f['unit'], isr=f['isr'],
                        before=prior_f['bytes'], new=f['bytes'],
                        saveRestoreBefore=prior_f['entryExitPushPopBytes'],
                        saveRestoreAfter=f['entryExitPushPopBytes']))
    built = [r for r in rows if r['status'] == 'BUILT']
    def reference(name):
        chosen = [r for r in built if r[name] is not None]
        return dict(count=len(chosen), mxBytes=sum(r['new'] for r in chosen),
                    referenceBytes=sum(r[name] for r in chosen),
                    at110=sum(r['new']*100 <= r[name]*110 for r in chosen),
                    at120=sum(r['new']*100 <= r[name]*120 for r in chosen),
                    new110=[r['id'] for r in chosen if r['new']*100 <= r[name]*110 < r['before']*100],
                    new120=[r['id'] for r in chosen if r['new']*100 <= r[name]*120 < r['before']*100],
                    medianPercent=statistics.median(100*r['new']/r[name] for r in chosen))
    tests = {name: read(after/'tests'/name/file) for name, file in
             [('objects','results.json'), ('symbol','result.json'), ('cfg','result.json')]}
    assert all(record['status'] == 'PASS' for record in tests.values())
    assert tests['objects']['compilerSha256'] == compiler
    hardware = read(after/'hardware-capture/result.json')
    assert hardware['status'] == 'PASS'
    assert read(after/'hardware-project/.build/mx251/latest/receipt.json')['compilerSha256'] == compiler
    assert pin(after/'hardware-project/.build/mx251/latest/firmware.hex')['sha256'] == hardware['firmwareSha256']
    return dict(schema=1, implementationCommit=core['sourceCommit'], compilerSha256=compiler,
                parentReport=pin(before/'comparison.json'), sourceAlgorithmsChanged=False,
                rows=rows, changedFunctions=functions, changedIsrBodies=body_changes,
                resolved=resolved, remaining=remaining,
                summary=dict(projects=len(rows), fullRebuilds=len(built),
                    statuses=dict(Counter(r['status'] for r in rows)),
                    beforeBytes=sum(r['before'] for r in built), afterBytes=sum(r['new'] for r in built),
                    reduced=sum(r['savedBytes'] > 0 for r in built),
                    unchanged=sum(r['savedBytes'] == 0 for r in built),
                    increased=sum(r['savedBytes'] < 0 for r in built),
                    resolvedIsrs=len(resolved), remainingCallBlockedIsrs=len(remaining),
                    keil=reference('keil'), stc=reference('stc')),
                tests=tests, hardware=dict(status=hardware['status'], cases=hardware['cases'],
                    frames=hardware['frames'], firmwareSha256=hardware['firmwareSha256'],
                    evidence=pin(after/'hardware-capture/result.json'),
                    source=pin(after/'hardware-project/source.json')),
                timing='Interrupt context and result correctness tested; engineering runtime ratios not measured.')


def markdown(result, after):
    s = result['summary']; h = result['hardware']
    text = ['# EDATA 对象与 PSW 标志位的通用调用分析', '',
        f"实现提交：`{result['implementationCommit']}`。编译器 SHA-256：`{result['compilerSha256']}`。", '',
        '编译器为实际分配的 EDATA 对象输出空间和长度，调用分析利用链接器已有的 '
        '[0x100, stackBase) 分配约束识别 e: 写入及符号地址；跨文件定义与同名 static 按原有绑定规则处理。'
        'PSW 的六个非银行选择位支持数字、声明别名及 PSW.bit 写法，RS0/RS1 和整字节 PSW 写入维持原处理。'
        '没有增加器件开关，也没有改动 SDK 源码或算法。', '',
        f"97 个可构建工程全部通过候选核心公开入口重编，同源配置逐字节核对。合计 "
        f"{s['beforeBytes']} → {s['afterBytes']} B，减少 {s['beforeBytes']-s['afterBytes']} B；"
        f"{s['reduced']} 项缩小、{s['unchanged']} 项相同、{s['increased']} 项增大。", '',
        f"原调用分析阻塞的 27 个 ISR 中，本轮解开 {s['resolvedIsrs']} 个，剩余 {s['remainingCallBlockedIsrs']} 个。"
        'USB 深层指针及间接跳转未扩大实现。', '',
        '前期只按中断保存估计300 B；普通调用还受益于F0识别及EDATA信息：'
        'EEPROM/I²C调用减少现场保存，部分红外函数不再需要额外的函数入口保存。'
        'g123的SCSI普通调用减少7 B；g133的EEPROM、发送调用及布局合计减少62 B。'
        '这些变化来自同一通用分析，没有增加针对例程的优化。', '',
        '所有ISR函数体在归一化调用/跳转宽度后与父版相同；本轮中断收益来自入口/出口保存缩减。'
        '普通函数还会重新分配跨调用活跃值，个别函数可略增大，工程总量均未增大。', '',
        '| 参考 | 可比工程 | ≤110% | ≤120% | MX251 / 参考合计 B |',
        '| --- | ---: | ---: | ---: | ---: |']
    for name, title in [('keil','Keil C251'), ('stc','STC-IDE')]:
        r = s[name]
        text.append(f"| {title} | {r['count']} | {r['at110']} | {r['at120']} | {r['mxBytes']} / {r['referenceBytes']} |")
    text += ['', 'Keil/STC-IDE 使用冻结的同源结果。5 项 OMF 未支持、1 项原始源码错误和39项纯汇编状态沿用上一轮，未重新构建。', '',
        '通过127项真实 C 对象/跨文件/位操作检查（包含父版与新版反馈前 REL 一致性），'
        '以及13项符号地址、194项算术传播、3项分配绑定、22项 CFG 检查，共359项。', '',
        f"AI8051U 实板 {h['cases']} 组、{h['frames']} 帧通过。Timer0 调用独立单元内的 EDATA 8/16/32位写入函数及 F0 别名写入，"
        'Timer1 覆盖 XDATA；检查四个寄存器银行中的 R0～R31、DPX、PSW/PSW1、SPX和计算结果。'
        '未测完整工程运行时间，体积数据不代表双110%目标已达成。', '',
        f"候选入口：`{after/'Build-Optimized.ps1'}`。冻结发行基线未替换。", '',
        '| 工程 | 父版 B | 新版 B | 减少 B | 相对 Keil |',
        '| --- | ---: | ---: | ---: | ---: |']
    for r in result['rows']:
        if r.get('savedBytes'):
            text.append(f"| {r['id']} {r['name']} | {r['before']} | {r['new']} | {r['savedBytes']} | {100*r['new']/r['keil']:.2f}% |")
    return '\n'.join(text)+'\n'


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    a = p.parse_args()
    result = compare(a.before, a.after)
    save(a.after/'comparison.json', result)
    (a.after/'comparison.md').write_text(markdown(result, a.after), encoding='utf-8', newline='\n')
    print(result['summary'])
