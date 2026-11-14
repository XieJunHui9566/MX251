"""Check typed EDATA costs and whole-function choices against assembled output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'baseline', 'support', 'output'):
    p.add_argument('--'+key, required=True, type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
source = (tests/'body-choice-cases.c').read_text().replace(
    'void apply_feature(void)', '''struct record { u8 head; u16 count; u8 tail; };
volatile struct record __edata records[2];
volatile u16 __edata __at(0x2000) fixed_word;
extern volatile u16 __edata external_word;
void apply_feature(void) __sdcccall(TEST_ABI)''').replace(
    '    u8 endpoint;', '    u8 endpoint;\n    records[1].count = 0x1234;')
source += '''
u16 edata_spaces(void) __sdcccall(TEST_ABI)
{
    records[0].head = device;
    fixed_word = external_word;
    return records[1].count + fixed_word;
}
'''
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
rows = []
for abi in (2,4):
    for model in ('small','large'):
        for opt in ('size','speed'):
            d = a.output/f'abi{abi}-{model}-{opt}'; d.mkdir()
            (d/'body-choice-cases.c').write_text(source)
            shutil.copy2(tests/'machine-board.h', d/'machine-board.h')
            costs = {}
            for label, cc in (('parent',a.baseline),('new',a.sdcc)):
                cmd = [str(cc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                       '--stack-auto','--mcs251-wide-ordinary','--mcs251-native-abi',
                       '--mcs251-machine-ir','--mcs251-function-metadata','-DTEST_ABI='+str(abi),
                       '-c','body-choice-cases.c','-o',label+'.rel']
                q = subprocess.run(cmd,cwd=d,env=env,capture_output=True)
                (d/(label+'.log')).write_bytes(q.stdout+q.stderr)
                assert q.returncode == 0, (d,label,q.stderr)
                text = (d/(label+'.asm')).read_text()
                costs[label] = {}
                for fn in ('apply_feature','edata_spaces'):
                    symbol = ('_' if abi==2 else '___m251r4_')+fn
                    m = re.search(r'; mx251-backend-choice function='+symbol+r' (.*)',text)
                    assert m, (d,label,fn)
                    costs[label][fn] = dict(re.findall(r'([\w-]+)=([^\s]+)',m[1]))
                # Each EDATA transfer is measured by the assembler's addresses.
                ins = [(int(m[1],16),line) for line in (d/(label+'.lst')).read_text().splitlines()
                       if (m:=re.match(r'^\s{6}([0-9A-F]{6}) .*\[\d+\]',line))]
                n = 0
                for (at,line),(after,_) in zip(ins,ins[1:]):
                    if re.search(r'\bmov\s+.*e:',line):
                        assert after-at == 4, (d,label,line,after-at)
                        n += 1
                assert n, (d,label)
                costs[label]['assembledEdataMoves'] = n
            for fn in ('apply_feature','edata_spaces'):
                old,new = costs['parent'][fn],costs['new'][fn]
                missing = int(old['unknown'].split('/')[0])
                assert missing > 0 and new['unknown']=='0/0', (d,fn,old,new)
                assert int(new['wide-bytes']) == int(old['wide-bytes'])+4*missing, (d,fn)
                assert int(new['wide-ins']) == int(old['wide-ins'])+missing, (d,fn)
            assert costs['parent']['apply_feature']['selected']=='wide'
            assert costs['new']['apply_feature']['selected']=='legacy', (d,costs)
            rows.append(dict(abi=abi,model=model,optimization=opt,costs=costs))
report=dict(status='PASS', compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
            baselineSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest(),
            sourceSha256=hashlib.sha256(source.encode()).hexdigest(),configurations=rows)
(a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 8 configurations, EDATA array/member/absolute/extern costs, assembled byte lengths and backend choices')
