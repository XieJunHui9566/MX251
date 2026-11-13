"""Rebuild math and DATA-overflow projects with one candidate compiler."""
import argparse
import concurrent.futures
from pathlib import Path
import sdk_coverage_matrix as matrix
from sdk_coverage_followups import clone_project

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--coverage',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--core',type=Path,required=True)
a=p.parse_args()
config=matrix.read(a.coverage/'config.json')
config.update(output=str(a.output.resolve()),core=str(a.core.resolve()))
a.output.mkdir(parents=True,exist_ok=True)
matrix.save(a.output/'config.json',config)
tasks=[]
math_ids=('p041','p076','p077','p079','p081')
edata_ids=('p021','p024','p030','p043','p068','p069','p116')
for project in matrix.read(a.coverage/'inputs.json')['projects']:
    variants=[]
    if project['id'] in math_ids:
        variants=[('mx251-native',dict(runtime='native-math')),
                  ('mx251-standard',dict(runtime='standard'))]
    if project['id'] in edata_ids:
        variants=[('mx251-edata',dict(defaultDataSpace='edata'))]
    for suite, settings in variants:
        source=a.coverage/'projects'/project['id']/'mx251'
        dest=a.output/'projects'/project['id']/suite
        clone_project(source,dest,**settings)
        local=dict(config)
        if settings.get('runtime')=='native-math':
            local['mxSwitches']=[s for s in config['mxSwitches'] if s!='-NativeIntegerRuntime']
        tasks.append((local,project,suite))
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    rows=list(pool.map(lambda args:matrix.one(*args),tasks))
matrix.save(a.output/'results.json',rows)
