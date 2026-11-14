"""Compare a common-architecture build with the frozen STC32G references."""
import argparse
from collections import Counter
from pathlib import Path
from statistics import median
import subprocess
from unified_sdk_matrix import read, save, pin


def pairs(rows, key):
    selected = [r for r in rows if r['new'] is not None and r[key] is not None]
    return dict(count=len(selected), mxBytes=sum(r['new'] for r in selected),
                referenceBytes=sum(r[key] for r in selected),
                at110=sum(r['new']*100 <= r[key]*110 for r in selected),
                at120=sum(r['new']*100 <= r[key]*120 for r in selected),
                medianPercent=median(100*r['new']/r[key] for r in selected))


def report(root, reference, output):
    projects = read(root/'inputs.json')['projects']
    rows = []
    status = Counter()
    for p in projects:
        key = p['id']
        old = read(reference/'projects'/key/'mx251/result.json')
        new = read(root/'projects'/key/'mx251/result.json')
        k = read(reference/'projects'/key/'keil/result.json')
        s = read(reference/'projects'/key/'stc-soft/result.json')
        status[new['status']] += 1
        row = dict(id=key,name=p['name'],beforeStatus=old['status'],status=new['status'],
                   before=old.get('payloadBytes'),new=new.get('payloadBytes'),
                   keil=k.get('payloadBytes'),stc=s.get('payloadBytes'),
                   inputAdaptation=bool(read(root/'projects'/key/'input-provenance.json')['redirects']),
                   result=pin(root/'projects'/key/'mx251/result.json'))
        if new['status']=='BUILT':
            latest=root/'projects'/key/'mx251/.build/mx251/latest'
            receipt=read(latest/'receipt.json')
            assert receipt['nativeCodeReads']['enabled']
            assert receipt['interruptVectorCount']==0
            assert receipt['targetContract']['architecture']=='mcs251-flat24-codeff'
            assert receipt['interruptClosure']['finalVerified']
            assert pin(latest/'firmware.hex')['sha256']==new['hex']['sha256']==receipt['firmwareSha256']
        rows.append(row)
    common=[r for r in rows if r['before'] is not None and r['new'] is not None]
    fixed=[r['id'] for r in rows if r['before'] is None and r['new'] is not None]
    ai=read(root/'ai-results.json')
    controls=[]
    for key in sorted({r['id'] for r in ai}):
        group={r['mode']:r['result'] for r in ai if r['id']==key}
        assert set(group)=={'parent','candidate'} and all(r['status']=='BUILT' for r in group.values())
        controls.append(dict(id=key,before=group['parent']['payloadBytes'],after=group['candidate']['payloadBytes'],
                             identicalHex=group['parent']['hex']['sha256']==group['candidate']['hex']['sha256']))
    totals=dict(projects=len(rows),statuses=dict(status),fixed=fixed,
                commonProjects=len(common),beforeBytes=sum(r['before'] for r in common),
                afterBytes=sum(r['new'] for r in common),
                reduced=sum(r['new']<r['before'] for r in common),
                unchanged=sum(r['new']==r['before'] for r in common),
                increased=sum(r['new']>r['before'] for r in common),
                keil=pairs(rows,'keil'),stc=pairs(rows,'stc'))
    result=dict(schema=1,sourceCommit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                core=pin(root/'candidate/core/core.json'),reference=pin(reference/'inputs.json'),
                summary=totals,aiControls=controls,rows=rows,hardwareExecuted=False,
                compilerBinaryUnchanged=True,sourceAlgorithmsChanged=False,
                measurement='HEX emitted bytes; unchanged Keil and STC-IDE references reused; no timing measurements')
    save(output.with_suffix('.json'),result)
    lines=['# 通用架构能力与 STC32G 优化结果','',
           '本轮将编译能力从器件的独立开关上移到架构配置。CPU 架构和 ABI 决定通用代码生成，器件配置描述内存、启动、SFR 和外设效果。SDK 算法保持原样，编译器二进制沿用当前核心，本轮改动位于正式构建链与 C251 兼容层。','',
           '## 结果','',
           f"STC32G 全部 {len(rows)} 个 C 工程：{status['BUILT']} 个成功；修复原来的 {len(fixed)} 个 MX251 构建失败。5 个 USB OMF 库工程和 1 个官方源码错误仍未完成。39 个纯 A251 汇编工程本轮未重跑，仍沿用此前未支持的结论。",'',
           f"新旧均成功的 {len(common)} 项：{totals['beforeBytes']} → {totals['afterBytes']} B，减少 {totals['beforeBytes']-totals['afterBytes']} B；{totals['reduced']} 项缩小、{totals['unchanged']} 项相同、{totals['increased']} 项增大。新增成功工程不混入该节省量。",'',
           '| 参考 | 可比工程 | ≤110% | ≤120% | MX251 / 参考合计 B | 比值中位数 |',
           '| --- | ---: | ---: | ---: | ---: | ---: |']
    for key,label in [('keil','Keil C251'),('stc','STC-IDE')]:
        t=totals[key]
        lines.append(f"| {label} | {t['count']} | {t['at110']} | {t['at120']} | {t['mxBytes']} / {t['referenceBytes']} | {t['medianPercent']:.2f}% |")
    lines+=['',f"AI8051U 六个对照工程 {', '.join(r['id'] for r in controls)} 分别使用旧核心和本候选重新编译；{sum(r['identicalHex'] for r in controls)}/{len(controls)} 个 HEX 完全相同。本轮仅统计体积和编译结果，未测整工程运行时间。",'',
            '## 实现','',
            '- TargetContracts schema 2 增加架构层；两种器件复用 mcs251-flat24-codeff 的 CODE 原生读取、对象地址能力与按程序生成的向量表。未来同架构目标自动继承，不需要复制优化开关。',
            '- CLI/CMake 从目标注册表选择 profile，移除入口中两个器件名的白名单；SPH、外设位效果、启动与内存窗口仍由器件描述。STC32G 的 SPH=0x85 来自官方 STC32G.H，Keil 的正弦表读取也使用 MOV WR,@DR。',
            '- 向量表按实际链接的 ISR 生成；保留跨文件、归档成员的 ISR 发现与芯片槽号上限。未使用的中间槽采用现有 compiler-default RETI，最高已用槽之后不再预留中断表；不再默认生成全槽停机桩。',
            '- C251 兼容层沿共享标签跳转链解析中断转发，生成裸 ISR。7 个 DMA 工程不再由旧转换额外占据固定 HOME 地址。原始汇编和转换差异保存在 input-provenance.json；没有改 C 算法。',
            '- 近跳转支持只有局部跳转、没有可转换函数调用的计划，修复空函数列表的 PowerShell 属性访问错误。','',
            '## 验证与使用','',
            '通用能力继承检查覆盖两个真实目标及一个独立名称的配置；另有跨文件中断发现与槽号上限、16 项近跳转选择、6 项页内跳转布局、数字/共享标签转发与 8 类不支持输入检查。真实工程最终向量字节和 HEX 哈希随构建记录核对。','',
            f'本地开发入口：`{root.as_posix()}/Build-Optimized.ps1`。使用基线自带 PowerShell 调用，传入 `-ProjectPath <工程>`，Keil 兼容工程追加 `-C251Compatibility`。候选尚未替换 baseline-20261114 标签。','',
            '逐项结果：','',
            '| ID | 旧 MX B | 新 MX B | Keil B | 新/Keil | STC-IDE B | 状态 |',
            '| --- | ---: | ---: | ---: | ---: | ---: | --- |']
    for r in rows:
        ratio=f"{100*r['new']/r['keil']:.2f}%" if r['new'] is not None and r['keil'] else '—'
        lines.append(f"| {r['id']} | {r['before'] if r['before'] is not None else '—'} | {r['new'] if r['new'] is not None else '—'} | {r['keil'] if r['keil'] is not None else '—'} | {ratio} | {r['stc'] if r['stc'] is not None else '—'} | {r['status']} |")
    output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(totals)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',required=True,type=Path);p.add_argument('--reference',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();report(a.root,a.reference,a.output)
