"""Record complete linked gains and carry forward the frozen 203-project matrix."""
import argparse
from copy import deepcopy
from pathlib import Path
from unified_sdk_matrix import read,save,pin
from call_layout import ihex
from focus_size_band import functions

p=argparse.ArgumentParser(description=__doc__)
for key in ('root','parent','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();root=a.root.resolve();parent=read(a.parent)
before={r['id']:deepcopy(r) for r in parent['rows']};updated=deepcopy(before)
core=root/'candidate/core';meta=read(core/'core.json');compiler=meta['compiler']['sha256']
census=read(root/'census-final/result.json')
assert census['failed']==0 and census['compiler']['sha256']==compiler
assert read(root/'contracts-qualified/result.json')['compilerSha256']==compiler
hw=read(root/'hardware-final-capture/result.json');assert hw['status']=='PASS'
receipt=read(root/'hardware-final/.build/mx251/latest/receipt.json')
assert receipt['compilerSha256']==compiler and receipt['firmwareSha256']==hw['firmwareSha256']
rows=[]
for pid in census['changedProjects']:
    old=before[pid];previous=Path(old['result']['path']).parent;current=root/'projects'/pid/'mx251'
    result=read(current/'result.json');assert result['status']=='BUILT'
    for source in list((previous/'src').rglob('*'))+[previous/n for n in ('mx251.json','CMakeLists.txt','dialect.json')]:
        if source.is_file():assert source.read_bytes()==(current/source.relative_to(previous)).read_bytes(),source
    latest=current/'.build/mx251/latest';assert read(latest/'receipt.json')['compilerSha256']==compiler
    after=result['payloadBytes'];k=old['references']['keil']['bytes']
    bf,_=functions(previous/'.build/mx251/latest',ihex(previous/'.build/mx251/latest/firmware.hex'))
    af,_=functions(latest,ihex(latest/'firmware.hex'));bf={f['name']:f for f in bf}
    changes=[dict(name=f['name'],before=bf[f['name']]['bytes'],after=f['bytes'],isr=f['isr']) for f in af
             if f['name'] in bf and f['bytes']!=bf[f['name']]['bytes']]
    row=dict(id=pid,name=old['name'],cohort=old['cohort'],before=old['bytes'],after=after,
             saved=old['bytes']-after,keilBytes=k,beforeRatio=old['bytes']/k,afterRatio=after/k,
             crossed110=10*old['bytes']>11*k and 10*after<=11*k,
             crossed120=5*old['bytes']>6*k and 5*after<=6*k,
             sourceInputsIdentical=True,functions=changes,result=pin(current/'result.json'))
    rows.append(row);updated[pid].update(bytes=after,result=row['result'],receipt=pin(latest/'receipt.json'),
        hex=pin(latest/'firmware.hex'),buildWallSeconds=result['wallSeconds'])
summaries={}
for cohort in sorted({r['cohort'] for r in updated.values()}):
    group=[r for r in updated.values() if r['cohort']==cohort]
    stats=dict(projects=len(group),keil110=sum(10*r['bytes']<=11*r['references']['keil']['bytes'] for r in group),
               keil120=sum(5*r['bytes']<=6*r['references']['keil']['bytes'] for r in group))
    vendor=[r for r in group if r['references'].get('stc-soft',{}).get('status')=='BUILT']
    stats.update(stcProjects=len(vendor),stc110=sum(10*r['bytes']<=11*r['references']['stc-soft']['bytes'] for r in vendor),
                 stc120=sum(5*r['bytes']<=6*r['references']['stc-soft']['bytes'] for r in vendor))
    summaries[cohort]=stats
save(root/'cumulative-comparison.json',dict(rows=list(updated.values()),counts=summaries,
    method='Retained cumulative matrix plus full linked rebuilds of projects with selected caller-save reuse.'))
report=dict(schema=1,status='PASS_CALL_SAVE_REUSE',sourceCommit=meta['compilerSourceCommit'],
    compiler=pin(core/'toolchain/bin/sdcc.exe'),core=str(core),buildEntry=str(root/'Build-Optimized.ps1'),parent=pin(a.parent),
    scannedProjects=len({r['id'] for r in census['rows']}),scannedUnits=census['units'],
    rebuiltProjects=len(rows),improvedProjects=sum(r['saved']>0 for r in rows),
    unchangedProjects=sum(r['saved']==0 for r in rows),largerProjects=sum(r['saved']<0 for r in rows),
    savedBytes=sum(r['saved'] for r in rows),rows=rows,counts=summaries,
    new110=[r['id'] for r in rows if r['crossed110']],new120=[r['id'] for r in rows if r['crossed120']],
    hardware=dict(cases=hw['cases'],frames=hw['frames'],port=hw['port'],firmwareSha256=hw['firmwareSha256'],
      codeBytes=receipt['codeBytes'],checks=['ABI2/ABI4 consecutive calls with byte/word/dword live values',
      'register arguments, stack arguments and later use of saved values as arguments',
      'all four interrupted register banks; GPR, PSW/PSW1, DPX and SPX restoration',
      'local stack array and unknown assembly callee']),
    limitations=['Official project runtime was not measured; instruction counts are not cycle measurements.',
      'Keil/STC-IDE reference images are retained builds; unchanged projects retain previous linked images.',
      'Reuse currently requires identical complete save layouts and straight-line register-only gaps.',
      'Ordered inline assembly and deferred word-multiply replacement regions do not participate.'],
    evidence=[pin(root/path) for path in ['census-final/result.json','contracts-qualified/result.json',
      'hardware-final/source.json','hardware-final/.build/mx251/latest/receipt.json',
      'hardware-final-capture/result.json','package-check.json','cumulative-comparison.json']])
save(a.output,report)
meta.update(qualification='PASS_CALL_SAVE_REUSE',hardwareExecuted=True,
    hardwareReport=pin(root/'hardware-final-capture/result.json'),callSaveReuseQualification=pin(a.output))
save(core/'core.json',meta)
print(report['improvedProjects'],report['savedBytes'],report['new110'],report['new120'],summaries)
