"""Record linked size gains and qualification of the native bank0 ISR path."""
import argparse
from pathlib import Path
from copy import deepcopy
import subprocess
from call_layout import ihex
from focus_size_band import functions
from unified_sdk_matrix import read,save,pin

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--matrix',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
baseline=read(a.matrix/'comparison.json');old={r['id']:r for r in baseline['rows']}
core=root/'candidate/core';meta=read(core/'core.json');compiler=meta['compiler']['sha256']
census=read(root/'final-census/result.json');equivalence=read(root/'compiler-equivalence.json')
assert census['failed']==0 and census['compilerSha256']==compiler
assert equivalence['status']=='PASS' and equivalence['finalCompilerSha256']==compiler
assert read(root/'contracts/result.json')['compilerSha256']==compiler
assert read(root/'abi-regression/results.json')['compilerSha256']==compiler
hardware=read(root/'hardware-capture-final/result.json')
receipt=read(root/'hardware-final/.build/mx251/latest/receipt.json')
assert hardware['status']=='PASS' and receipt['compilerSha256']==compiler
assert hardware['firmwareSha256']==pin(root/'hardware-final/.build/mx251/latest/firmware.hex')['sha256']
rows=[];updated=deepcopy(baseline['rows'])
for result in read(root/'pilot-builds.json'):
    assert result['status']=='BUILT',result
    pid=result['id'];before=old[pid];project=root/'projects'/pid/'mx251'
    previous=Path(before['result']['path']).parent
    for source in list((previous/'src').rglob('*'))+[previous/n for n in ('mx251.json','CMakeLists.txt','dialect.json')]:
        if source.is_file():assert source.read_bytes()==(project/source.relative_to(previous)).read_bytes(),source
    after=result['payloadBytes'];assert after<before['bytes']
    keil=before['references']['keil']['bytes']
    bfunc,_=functions(previous/'.build/mx251/latest',ihex(previous/'.build/mx251/latest/firmware.hex'))
    afunc,_=functions(project/'.build/mx251/latest',ihex(project/'.build/mx251/latest/firmware.hex'))
    bfunc={r['name']:r for r in bfunc}
    changes=[dict(name=f['name'],before=bfunc[f['name']]['bytes'],after=f['bytes']) for f in afunc
             if f['isr'] and f['name'] in bfunc and f['bytes']!=bfunc[f['name']]['bytes']]
    row=dict(id=pid,name=before['name'],before=before['bytes'],after=after,saved=before['bytes']-after,
             keilBytes=keil,beforeRatio=before['bytes']/keil,afterRatio=after/keil,
             crossed110=before['bytes']>1.1*keil and after<=1.1*keil,
             crossed120=before['bytes']>1.2*keil and after<=1.2*keil,
             sourceInputsIdentical=True,model=before['configuration']['model'],
             isrFunctions=changes,result=pin(project/'result.json'),references=before['references'])
    rows.append(row)
    next(x for x in updated if x['id']==pid)['bytes']=after
summaries={}
for cohort in sorted({r['cohort'] for r in updated}):
    group=[r for r in updated if r['cohort']==cohort]
    summaries[cohort]=dict(projects=len(group),keil110=sum(r['bytes']<=1.1*r['references']['keil']['bytes'] for r in group),
                          keil120=sum(r['bytes']<=1.2*r['references']['keil']['bytes'] for r in group))
evidence=['final-census/result.json','compiler-equivalence.json','pilot-builds.json',
          'contracts/result.json','abi-regression/results.json','hardware-final/source.json',
          'hardware-final/.build/mx251/latest/receipt.json','hardware-capture-final/result.json']
report=dict(schema=1,status='PASS_NATIVE_BANK0_ISR_BODIES',sourceCommit=meta['compilerSourceCommit'],
            core=str(core),buildEntry=str(root/'Build-Optimized.ps1'),compiler=pin(core/'toolchain/bin/sdcc.exe'),
            previousMatrix=pin(a.matrix/'comparison.json'),sourceAlgorithmsChanged=False,
            isrProjectsScanned=len({r['id'] for r in census['rows']}),translationUnits=census['units'],
            nativeIsrFunctions=sum(v=='wide' for r in census['rows'] for n,v in r['choices']),
            fullyRebuiltProjects=len(rows),savedBytes=sum(r['saved'] for r in rows),rows=rows,
            cumulativeKeilCounts=summaries,cumulativeCountsMethod='Replace only these nine rows of the retained 203-project matrix.',
            hardware=dict(cases=hardware['cases'],frames=hardware['frames'],port=hardware['port'],
                          firmwareSha256=hardware['firmwareSha256'],codeBytes=receipt['codeBytes'],
                          checks=['all four interrupted register banks','R0..R31','PSW/PSW1','DPX','SPX',
                                  'early return','local volatile array','stack arguments','unknown assembly callee',
                                  'integer results against independent Python constants']),
            limitations=['Official project execution time was not measured; static instruction count is not cycle time.',
                         'Keil and STC-IDE binaries are retained reference builds, not rebuilt this round.',
                         'The nine project links used the pilot compiler; all 233 ISR-unit ASM and REL outputs are byte-identical with the final compiler.',
                         'Other-bank, naked, critical and other excluded entry contracts retain the legacy backend.',
                         'No complete new 203-project link matrix was run.'],
            evidence=[pin(root/rel) for rel in evidence])
save(a.output,report)
meta.update(qualification='PASS_NATIVE_BANK0_ISR_BODIES',hardwareExecuted=True,
            hardwareReport=pin(root/'hardware-capture-final/result.json'),nativeIsrQualification=pin(a.output))
save(core/'core.json',meta)
print(report['savedBytes'],report['cumulativeKeilCounts'])
for r in rows:print(r['id'],r['isrFunctions'])
