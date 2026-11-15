"""Record complete linked gains and carry forward the frozen 203-project matrix."""
import argparse
from copy import deepcopy
from pathlib import Path
from unified_sdk_matrix import read,save,pin
from call_layout import ihex
from focus_size_band import functions

p=argparse.ArgumentParser(description=__doc__)
for key in ('root','parent','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output=a.output.resolve();root=a.root.resolve();parent=read(a.parent)
before={r['id']:deepcopy(r) for r in parent['rows']};updated=deepcopy(before)
core=root/'candidate/core';meta=read(core/'core.json');compiler=meta['compiler']['sha256']
census=read(root/'census/result.json')
assert census['failed']==0 and census['compiler']['sha256']==compiler
contracts=read(root/'contracts-final/result.json')
assert contracts['status']=='PASS' and contracts['compilerSha256']==compiler
package=read(root/'package-check.json')
assert package['status']=='PASS' and package['compilerSha256']==compiler
hw=read(root/'capture-after-final/result.json');assert hw['status']=='PASS'
hw_before=read(root/'capture-before-final/result.json');assert hw_before['status']=='PASS'
receipt=read(root/'hardware-after/.build/mx251/latest/receipt.json')
receipt_before=read(root/'hardware-before/.build/mx251/latest/receipt.json')
assert receipt['compilerSha256']==compiler and receipt['firmwareSha256']==hw['firmwareSha256']
assert receipt_before['firmwareSha256']==hw_before['firmwareSha256']
rows=[]
for pid in census['changedProjects']:
    old=before[pid];previous=Path(old['result']['path']).parent;current=root/'projects'/pid/'mx251'
    result=read(current/'result.json');assert result['status']=='BUILT'
    for source in list((previous/'src').rglob('*'))+[previous/n for n in ('mx251.json','CMakeLists.txt','dialect.json')]:
        if source.is_file():assert source.read_bytes()==(current/source.relative_to(previous)).read_bytes(),source
    latest=current/'.build/mx251/latest';assert read(latest/'receipt.json')['compilerSha256']==compiler
    after=result['payloadBytes'];k=old['references']['keil']['bytes']
    bf,_=functions(previous/'.build/mx251/latest',ihex(previous/'.build/mx251/latest/firmware.hex'))
    af,_=functions(latest,ihex(latest/'firmware.hex'));bf={(f['unit'],f['name']):f for f in bf}
    changes=[dict(name=f['name'],unit=f['unit'],before=bf[f['unit'],f['name']]['bytes'],
                  after=f['bytes'],isr=f['isr']) for f in af
             if (f['unit'],f['name']) in bf and f['bytes']!=bf[f['unit'],f['name']]['bytes']]
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
    reference='stc' if cohort=='ai8051u-frozen75' else 'stc-soft'
    vendor=[r for r in group if r['references'].get(reference,{}).get('status')=='BUILT']
    stats.update(stcReference=reference,stcProjects=len(vendor),
                 stc110=sum(10*r['bytes']<=11*r['references'][reference]['bytes'] for r in vendor),
                 stc120=sum(5*r['bytes']<=6*r['references'][reference]['bytes'] for r in vendor))
    summaries[cohort]=stats
comparable=[r for r in updated.values() if r['id'] not in ('g083','p144','p145')]
qualified=dict(projects=len(comparable),
    keil110=sum(10*r['bytes']<=11*r['references']['keil']['bytes'] for r in comparable),
    keil120=sum(5*r['bytes']<=6*r['references']['keil']['bytes'] for r in comparable),
    excluded={'g083':'Known formatted-output width mismatch',
              'p144':'MX251 large model versus Keil XSMALL',
              'p145':'MX251 large model versus Keil XSMALL'})
save(root/'cumulative-comparison.json',dict(rows=list(updated.values()),counts=summaries,
    method='Retained cumulative matrix plus full linked rebuilds of projects with selected address rematerialization.'))
report=dict(schema=1,status='PASS_ADDRESS_REMATERIALIZATION',sourceCommit=meta['compilerSourceCommit'],
    compiler=pin(core/'toolchain/bin/sdcc.exe'),core=str(core),buildEntry=str(root/'Build-Optimized.ps1'),parent=pin(a.parent),
    scannedProjects=len({r['id'] for r in census['rows']}),scannedUnits=census['units'],
    rebuiltProjects=len(rows),improvedProjects=sum(r['saved']>0 for r in rows),
    unchangedProjects=sum(r['saved']==0 for r in rows),largerProjects=sum(r['saved']<0 for r in rows),
    savedBytes=sum(r['saved'] for r in rows),rows=rows,counts=summaries,comparableKeil=qualified,
    new110=[r['id'] for r in rows if r['crossed110']],new120=[r['id'] for r in rows if r['crossed120']],
    representativeFunctions=read(root/'representative-functions.json'),
    hardware=dict(cases=hw['cases'],frames=hw['frames'],port=hw['port'],firmwareSha256=hw['firmwareSha256'],
      codeBytes=receipt['codeBytes'],codeBytesBefore=receipt_before['codeBytes'],
      checks=['Structures, pointer snapshots, repeated address uses across calls and loops',
      'Aliased writes and branch-dependent pointer updates; 128 independent oracle cases'],
      baseline=pin(root/'capture-before-final/result.json'),
      ticksBefore=hw_before['aggregateTicks'],ticksAfter=hw['aggregateTicks']),
    limitations=['Official project runtime was not measured; instruction counts are not cycle measurements.',
      'Keil/STC-IDE reference images are retained builds; unchanged projects retain previous linked images.',
      'Address recipes currently cover plain scalar RAM/CODE accesses, preserving existing memory transactions.',
      'Whole-function weighted instruction cost is an estimate; paired hardware fixture times are recorded separately.'],
    evidence=[pin(root/path) for path in ['census/result.json','contracts-final/result.json',
      'capture-before-final/result.json','hardware-after/source.json','hardware-after/.build/mx251/latest/receipt.json',
      'capture-after-final/result.json','representative-functions.json','package-check.json','cumulative-comparison.json']])
save(a.output,report)
meta.update(qualification='PASS_ADDRESS_REMATERIALIZATION',hardwareExecuted=True,
    hardwareReport=pin(root/'capture-after-final/result.json'),addressRematerializationQualification=pin(a.output),
    report=str(a.output.with_suffix('.md')),sourceWasUncommittedAtStaging=False,
    stagedFromCommit=meta['compilerSourceCommit'],testSourceCommit=meta['compilerSourceCommit'])
save(core/'core.json',meta)
print(report['improvedProjects'],report['savedBytes'],report['new110'],report['new120'],summaries)
