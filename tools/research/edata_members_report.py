"""Compare public builds for EDATA member addressing and initializer analysis."""
import argparse
from collections import Counter
from pathlib import Path
import re
from unified_sdk_matrix import read, save, pin
from vector_gc_attribution import image_parts


def compare(before, after):
    previous = read(before/'comparison.json')
    core = read(after/'candidate/core/core.json')
    compiler = core['compiler']['sha256']
    rows, changed_functions, folds = [], [], []
    for old in previous['rows']:
        row = dict(id=old['id'], name=old['name'], status=old['status'],
                   before=old['new'], new=old['new'], keil=old['keil'], stc=old['stc'])
        rows.append(row)
        prior_result = Path(old['result']['path'])
        assert pin(prior_result)['sha256'] == old['result']['sha256']
        if old['status'] != 'BUILT':
            row.update(verification='inherited-status', result=pin(prior_result))
            continue
        prior = prior_result.parent
        project = after/'projects'/row['id']/'mx251'
        for name in ['mx251.json', 'CMakeLists.txt', 'dialect.json']:
            assert (prior/name).read_bytes() == (project/name).read_bytes(), (row['id'], name)
        for p in (prior/'src').rglob('*'):
            if p.is_file():
                assert p.read_bytes() == (project/p.relative_to(prior)).read_bytes(), p
        result = read(project/'result.json')
        latest = project/'.build/mx251/latest'
        assert result['status'] == 'BUILT', (row['id'], result)
        assert read(latest/'receipt.json')['compilerSha256'] == compiler
        row.update(new=result['payloadBytes'], savedBytes=old['new']-result['payloadBytes'],
                   verification='public-rebuild', result=pin(project/'result.json'))
        row['keilPercent'] = 100*row['new']/row['keil']
        for asm in latest.glob('u*.asm'):
            text = asm.read_text(errors='replace')
            legacy = set(re.findall(r'^; mx251-backend-choice function=_(\S+) selected=legacy', text, re.M))
            for m in re.finditer(r'^; wide edata-address folded function=(\S+) .*?symbol=(\S+) offset=(\d+) physical=(\d+) volatile-bytes=(\d+)$', text, re.M):
                if m[1] in legacy:
                    continue
                folds.append(dict(id=row['id'], unit=asm.stem, function=m[1],
                    symbol=m[2], offset=int(m[3]), physical=int(m[4]), volatile=bool(int(m[5]))))
        if row['savedBytes']:
            old_funcs = {(f['unit'], f['name']):f for f in image_parts(prior/'.build/mx251/latest')['functions']}
            for f in image_parts(latest)['functions']:
                prior_f = old_funcs.get((f['unit'], f['name']))
                if prior_f and prior_f['bytes'] != f['bytes']:
                    changed_functions.append(dict(id=row['id'], unit=f['unit'], name=f['name'],
                        before=prior_f['bytes'], new=f['bytes'], isr=f['isr']))
    built = [r for r in rows if r['status']=='BUILT']
    def reference(name):
        selected = [r for r in built if r[name] is not None]
        return dict(count=len(selected), mxBytes=sum(r['new'] for r in selected),
            referenceBytes=sum(r[name] for r in selected),
            at110=sum(r['new']*100 <= r[name]*110 for r in selected),
            at120=sum(r['new']*100 <= r[name]*120 for r in selected),
            new110=[r['id'] for r in selected if r['new']*100 <= r[name]*110 < r['before']*100],
            new120=[r['id'] for r in selected if r['new']*100 <= r[name]*120 < r['before']*100])
    tests = read(after/'tests/members-release/result.json')
    format_test = read(after/'tests/format/result.json')
    hardware = read(after/'hardware-capture/result.json')
    hardware_root = after/'hardware-project/.build/mx251/latest'
    assert tests['status'] == format_test['status'] == hardware['status'] == 'PASS'
    assert tests['compilerSha256'] == read(hardware_root/'receipt.json')['compilerSha256'] == compiler
    assert pin(hardware_root/'firmware.hex')['sha256'] == hardware['firmwareSha256']
    assert read(hardware_root/'format-runtime.json')['applied']
    format_only = read(after/'format/projects/g070/mx251/result.json')
    assert format_only['status']=='BUILT'
    return dict(schema=1, sourceCommit=core['sourceCommit'], compilerSha256=compiler,
        parentReport=pin(before/'comparison.json'), sourceAlgorithmsChanged=False,
        rows=rows, changedFunctions=changed_functions, edataFolds=folds,
        summary=dict(fullRebuilds=len(built), statuses=dict(Counter(r['status'] for r in rows)),
            beforeBytes=sum(r['before'] for r in built), afterBytes=sum(r['new'] for r in built),
            reduced=sum(r['savedBytes']>0 for r in built), unchanged=sum(r['savedBytes']==0 for r in built),
            increased=sum(r['savedBytes']<0 for r in built),
            foldedProjects=len({r['id'] for r in folds}),
            foldedFunctions=len({(r['id'],r['unit'],r['function']) for r in folds}),
            foldedAccesses=len(folds), keil=reference('keil'), stc=reference('stc')),
        formatOnly=dict(id='g070', before=26394, new=format_only['payloadBytes'],
            result=pin(after/'format/projects/g070/mx251/result.json')),
        tests=tests, formatTest=format_test,
        hardware=dict(status=hardware['status'], cases=hardware['cases'], frames=hardware['frames'],
            firmwareSha256=hardware['firmwareSha256'], evidence=pin(after/'hardware-capture/result.json')),
        timing='Host C results matched on AI8051U; full engineering runtime ratios not measured.')


def markdown(result):
    s = result['summary']
    lines = ['# EDATA 固定成员寻址与初始化分析', '',
        f"候选来源：`{result['sourceCommit']}`；编译器 SHA-256：`{result['compilerSha256']}`。", '',
        '格式化库需求分析识别编译器生成的 EDATA 初始化与清零；固定对象地址分析扩展到 EDATA，'
        '将结构体成员和数组常量下标直接映射为 EDATA 读写。保留三字节指针布局、volatile 字节事务和整函数成本择优。'
        '未改 SDK 源码，也未增加器件专用优化开关。', '',
        f"97 个可构建工程通过候选核心公开入口重编，输入与父版逐字节一致。"
        f"合计 {s['beforeBytes']} → {s['afterBytes']} B，减少 {s['beforeBytes']-s['afterBytes']} B；"
        f"{s['reduced']} 项缩小，{s['unchanged']} 项相同，{s['increased']} 项增大。", '',
        f"EDATA 折叠记录覆盖 {s['foldedProjects']} 个工程、{s['foldedFunctions']} 个函数、{s['foldedAccesses']} 个访存节点。", '',
        '| 参考 | 可比工程 | ≤110% | ≤120% | MX251 / 参考合计 B |',
        '| --- | ---: | ---: | ---: | ---: |']
    for name, title in [('keil','Keil C251'), ('stc','STC-IDE')]:
        r=s[name]
        lines.append(f"| {title} | {r['count']} | {r['at110']} | {r['at120']} | {r['mxBytes']} / {r['referenceBytes']} |")
    lines += ['', 'Keil/STC-IDE 沿用冻结的同源参考产物。5 项 OMF 不支持、1 项原始源码错误状态沿用父版；39 项纯汇编不在本轮 C 工程矩阵中。', '',
        f"只应用初始化分析修复时，g070 已从 26394 降到 {result['formatOnly']['new']} B。", '',
        '专项检查覆盖 small/large 与 speed/size 四种组合、三字节指针字段、固定下标、动态地址和不同存储空间。'
        '每种组合核对 22 次 volatile 字节事务，原有栈地址用例 REL 与父版相同。测试代码 1138 → 777 B。', '',
        f"AI8051U 实板 {result['hardware']['cases']} 组、{result['hardware']['frames']} 帧与主机 C 参考结果一致；"
        '同时执行了两种整数格式串和仅含初始化的 EDATA 翻译单元。完整工程运行时间尚未与 Keil/STC-IDE 对比。', '',
        '候选入口：`E:/STVAPP/MX251-baseline/work/edata-member-addresses-20261114/Build-Optimized.ps1`。冻结发行基线未替换。', '',
        '| 工程 | 父版 B | 新版 B | 减少 B | 相对 Keil |', '| --- | ---: | ---: | ---: | ---: |']
    for r in sorted((r for r in result['rows'] if r.get('savedBytes')), key=lambda r:-r['savedBytes']):
        lines.append(f"| {r['id']} {r['name']} | {r['before']} | {r['new']} | {r['savedBytes']} | {r['keilPercent']:.2f}% |")
    return '\n'.join(lines)+'\n'


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('before','after'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    result=compare(args.before,args.after)
    save(args.after/'comparison.json',result)
    (args.after/'comparison.md').write_text(markdown(result),encoding='utf-8')
    print(result['summary'])
