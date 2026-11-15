"""Record linked gains from integer negation, IDATA and complete backend costs."""
import argparse
from copy import deepcopy
from pathlib import Path
from unified_sdk_matrix import read,save,pin
from call_layout import ihex
from focus_size_band import functions

p=argparse.ArgumentParser(description=__doc__)
for key in ('root','matrix','parent','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();root=a.root.resolve()
matrix=read(a.matrix/'comparison.json');parent=read(a.parent)
before={r['id']:deepcopy(r) for r in matrix['rows']}
def update_row(row,byte_count,result_pin):
    result=read(Path(result_pin['path']))
    latest=Path(result_pin['path']).parent/'.build/mx251/latest'
    row.update(bytes=byte_count,result=result_pin,receipt=pin(latest/'receipt.json'),
               hex=pin(latest/'firmware.hex'),buildWallSeconds=result['wallSeconds'])
for row in parent['rows']:
    update_row(before[row['id']],row['after'],row['result'])
updated=deepcopy(before)
core=root/'candidate/core';meta=read(core/'core.json');compiler=meta['compiler']['sha256']
census=read(root/'census/result.json')
assert census['failed']==0 and census['compilerSha256']==compiler
assert read(root/'contracts-final/result.json')['compilerSha256']==compiler
assert read(root/'data-regression-final/result.json')['status']=='PASS'
hw=read(root/'hardware-capture/result.json');assert hw['status']=='PASS'
receipt=read(root/'hardware-final/.build/mx251/latest/receipt.json')
assert receipt['compilerSha256']==compiler and receipt['firmwareSha256']==hw['firmwareSha256']
ids={Path(f).parts[0] for f in read(root/'changed-units.json')}
rows=[]
for pid in sorted(ids):
    result=read(root/'projects'/pid/'mx251/result.json');assert result['status']=='BUILT'
    old=before[pid];previous=Path(old['result']['path']).parent
    current=root/'projects'/pid/'mx251'
    for source in list((previous/'src').rglob('*'))+[previous/n for n in ('mx251.json','CMakeLists.txt','dialect.json')]:
        if source.is_file():assert source.read_bytes()==(current/source.relative_to(previous)).read_bytes(),source
    latest=current/'.build/mx251/latest'
    assert read(latest/'receipt.json')['compilerSha256']==compiler
    after=result['payloadBytes'];assert after<=old['bytes'],pid
    k=old['references']['keil']['bytes']
    bf,_=functions(previous/'.build/mx251/latest',ihex(previous/'.build/mx251/latest/firmware.hex'))
    af,_=functions(latest,ihex(latest/'firmware.hex'))
    bf={f['name']:f for f in bf}
    changes=[dict(name=f['name'],before=bf[f['name']]['bytes'],after=f['bytes'],isr=f['isr']) for f in af
             if f['name'] in bf and f['bytes']!=bf[f['name']]['bytes']]
    row=dict(id=pid,name=old['name'],cohort=old['cohort'],before=old['bytes'],after=after,
             saved=old['bytes']-after,keilBytes=k,beforeRatio=old['bytes']/k,afterRatio=after/k,
             crossed110=old['bytes']>1.1*k and after<=1.1*k,
             crossed120=old['bytes']>1.2*k and after<=1.2*k,
             sourceInputsIdentical=True,functions=changes,result=pin(current/'result.json'))
    rows.append(row);update_row(updated[pid],after,row['result'])
summaries={}
for cohort in sorted({r['cohort'] for r in updated.values()}):
    group=[r for r in updated.values() if r['cohort']==cohort]
    stats=dict(projects=len(group),keil110=sum(r['bytes']<=1.1*r['references']['keil']['bytes'] for r in group),
               keil120=sum(r['bytes']<=1.2*r['references']['keil']['bytes'] for r in group))
    vendor=[r for r in group if r['references'].get('stc-soft',{}).get('status')=='BUILT']
    stats.update(stcProjects=len(vendor),stc110=sum(r['bytes']<=1.1*r['references']['stc-soft']['bytes'] for r in vendor),
                 stc120=sum(r['bytes']<=1.2*r['references']['stc-soft']['bytes'] for r in vendor))
    summaries[cohort]=stats
save(root/'cumulative-comparison.json',dict(rows=list(updated.values()),counts=summaries,
    method='Retained 203-project matrix plus prior native-ISR updates and the 23 linked projects in this report.'))
report=dict(schema=1,status='PASS_IDATA_NEGATE',sourceCommit=meta['compilerSourceCommit'],
            compiler=pin(core/'toolchain/bin/sdcc.exe'),core=str(core),buildEntry=str(root/'Build-Optimized.ps1'),
            parent=pin(a.parent),matrix=pin(a.matrix/'comparison.json'),
            scannedProjects=len({r['id'] for r in census['rows']}),scannedUnits=census['units'],
            rebuiltProjects=len(rows),improvedProjects=sum(r['saved']>0 for r in rows),
            unchangedProjects=sum(r['saved']==0 for r in rows),savedBytes=sum(r['saved'] for r in rows),
            rows=rows,counts=summaries,new110=[r['id'] for r in rows if r['crossed110']],
            new120=[r['id'] for r in rows if r['crossed120']],
            hardware=dict(cases=hw['cases'],frames=hw['frames'],port=hw['port'],
                          firmwareSha256=hw['firmwareSha256'],codeBytes=receipt['codeBytes'],
                          idataRange='0x6d..0xec',checks=['fixed and dynamic IDATA access above 0x7f',
                          'volatile 16-bit array loads and stores','near-to-generic pointer conversion',
                          'mixed-sign 16/32-bit negation via ABI2 and ABI4',
                          'all four interrupted register banks','GPR, PSW/PSW1, DPX and SPX restoration',
                          'stack frame and unknown assembly callee']),
            limitations=['Official project runtime was not measured; static instruction counts are not cycle measurements.',
                        'Keil and STC-IDE reference images are retained builds.',
                        'The census covers ISR translation units; it is not a fresh link of every project.',
                        'IDATA pointer parameters and pointer objects still use the legacy entry path.',
                        'A signed 16-to-32 conversion split through _BitInt(24) remains a separate import gap.'],
            evidence=[pin(root/path) for path in ['census/result.json','changed-units.json',
                'contracts-final/result.json','data-regression-final/result.json','hardware-final/source.json',
                'hardware-final/.build/mx251/latest/receipt.json','hardware-capture/result.json',
                'package-check.json','cumulative-comparison.json']])
save(a.output,report)
meta.update(qualification='PASS_IDATA_NEGATE',hardwareExecuted=True,hardwareReport=pin(root/'hardware-capture/result.json'),
            idataNegateQualification=pin(a.output));save(core/'core.json',meta)
print(report['improvedProjects'],report['savedBytes'],report['new120'],summaries)
