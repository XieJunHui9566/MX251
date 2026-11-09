"""Compile DATA indexing in both models and retain the existing near ABI."""
import argparse,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('sdcc','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent;rows=[]
env=os.environ.copy();env['SDCC_HOME']=str(a.support.resolve());env['PATH']=str(a.support.resolve()/'bin')+';'+env['PATH']
extra='''#include "data-index.h"
u16 near_parameter(u16 __data *p,u8 i) { return p[i]; }
u16 wide_index(unsigned long i) { return indexed_words[i]; }
u16 fixed_index(void) { return indexed_words[3]; }
u16 static_index(u8 i) {
    static u16 __data local[4];
    local[i]=0x1234+i;return local[(i+1)&3];
}
struct packet { u32 signature; u8 payload[12]; };
static struct packet __data packet;
u32 aggregate_view(void) { return packet.signature; }
'''
for model in ('small','large'):
    for opt in ('speed','size'):
        d=a.output/(model+'-'+opt);d.mkdir()
        for name in ('machine-board.h','data-index.h','data-index-cases.c','data-index-objects.c'):shutil.copy2(tests/name,d/name)
        (d/'extra.c').write_text(extra)
        for source in ('data-index-cases','data-index-objects','extra'):
            cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata','-c',source+'.c']
            q=subprocess.run(cmd,cwd=d,capture_output=True,env=env)
            (d/(source+'.log')).write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,q.stderr
            rows.append(dict(command=cmd,output=str(d),exitCode=q.returncode))
        text=(d/'data-index-cases.asm').read_text()
        assert 'wide-mir accepted: data_index_case' in text
        assert 'function=_data_index_case selected=wide' in text
        for width in (1,2,4):
            for operation in ('load','store'):
                assert re.search(r'wide indexed DATA '+operation+r' .*physical='+str(width),text)
        assert re.search(r'wide indexed DATA load .*volatile=1',text)
        assert re.search(r'wide indexed DATA store .*volatile=1',text)
        assert re.search(r'wide indexed DATA address .*op=-',text)
        text=(d/'extra.asm').read_text()
        assert 'wide-mir accepted: near_parameter' not in text
        assert 'wide-mir accepted: wide_index' in text
        assert 'wide-mir accepted: static_index' in text
        assert 'wide-mir accepted: aggregate_view' not in text
        fixed=text.split('; mx251-function begin schema=1 symbol=_fixed_index ',1)[1].split('; mx251-function end',1)[0]
        assert 'wide indexed DATA address' not in fixed
        assert 'wide near DATA field' in fixed
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=4,commands=rows),indent=2)+'\n')
print('PASS: small/large x speed/size, DATA byte/word/dword, volatile, signed offsets, address merges, and unchanged near parameter ABI')
