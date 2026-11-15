"""Compile near IDATA accesses and mixed-sign negation in both models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
for name in ('sdcc','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
sys.path.insert(0,str(tests.parents[5]/'tools/research'))
import idata_negate_fixture
fixture=a.output/'fixture';idata_negate_fixture.create(fixture)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support),MX251_WIDE_VALUE_LIMIT='1024',MX251_WIDE_NODE_LIMIT='2048')
env['PATH']=str(a.support/'bin')+os.pathsep+env['PATH']
rows=[]
for model in ('small','large'):
    for opt in ('speed','size'):
        out=a.output/(model+'-'+opt);out.mkdir()
        for name in ('machine-board.h','data-index.h','data-index-cases.c','data-index-objects.c'):
            (out/name).write_text((tests/name).read_text().replace('__data','__idata'))
        for name in ('tail-boundary.h','call-boundary-isr.c','isr-helper.c'):
            (out/name).write_text((fixture/name).read_text())
        (out/'extra.c').write_text('''#include "data-index.h"
u16 near_parameter(u16 __idata *p,u8 i){return p[i];}
u16 fixed_index(void){return indexed_words[3];}
signed char negate8(unsigned char x){signed char v=x;return -v;}
int promote8(unsigned char x){return -x;}
''')
        for name in ('data-index-cases','data-index-objects','call-boundary-isr','isr-helper','extra'):
            cmd=[str(a.sdcc),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto',
                 '--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata',
                 '--mcs251-flat-code-reads','-c',name+'.c']
            q=subprocess.run(cmd,cwd=out,env=env,capture_output=True)
            (out/(name+'.log')).write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,(name,q.stderr)
        text=(out/'data-index-cases.asm').read_text()
        assert 'wide-mir accepted: data_index_case' in text
        for width in (1,2,4):
            for op in ('load','store'):
                assert re.search(r'wide indexed DATA '+op+r' .*physical='+str(width),text)
        text=(out/'call-boundary-isr.asm').read_text()
        for name in ('context_irq','context_leaf_irq'):
            assert f'mx251-isr-choice function=_{name} selected=wide' in text
            assert re.search(r'backend-choice function=_'+name+r' .*unknown=0/0',text)
        text=(out/'isr-helper.asm').read_text()
        for name in ('negate32_2','negate32_4','negate16_4','negate16_2'):
            assert 'wide-mir accepted: '+name in text
        text=(out/'extra.asm').read_text()
        assert 'wide-mir accepted: near_parameter' not in text
        assert 'wide-mir accepted: negate8' in text
        assert 'wide-mir accepted: promote8' in text
        body=text.split('; mx251-function begin schema=1 symbol=_fixed_index ',1)[1].split('; mx251-function end',1)[0]
        assert re.search(r'backend-choice function=_fixed_index .*wide-bytes=[1-9]',body)
        assert 'wide symbol load' not in body
        assert not re.search(r'\bmov\s+(?:a|r\d+|wr\d+|dr\d+),\(?_indexed_words',body)
        rows.append(dict(model=model,optimization=opt,units=5,nativeIsrs=2,
                         indexedWidths=[1,2,4],fixedIdataIndirect=True,nearParameterUnchanged=True))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),rows=rows),indent=2)+'\n')
print('PASS: 4 configurations, 20 units; IDATA fixed/indexed/volatile accesses, negation and ISR selection')
