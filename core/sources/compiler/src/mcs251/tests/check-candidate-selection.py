"""Exercise complete-body choices and mixed CODE/data loops without SDK sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'baseline', 'support', 'output'):
    p.add_argument('--'+key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = '''typedef unsigned char u8;
typedef unsigned int u16;
#define ABI __sdcccall(TEST_ABI)
const __code u8 pattern[32]={1,7,11,19}, mask[16]={1,2,4,8};
const __code u8 first[1024]={3,5,7,11}, second[1024]={13,17,19,23};
const __code u16 words[64]={101,257,1021,4093};
extern __data u8 state[16], cursor;
extern __xdata u8 buffer[1024];
extern volatile __bit strobe;
void send(u8 value) ABI;
void drain(void) ABI;
void scan(void) ABI {
    send(pattern[state[cursor]]); send(~mask[cursor]);
    strobe=1; strobe=0;
    if(++cursor>=16)cursor=0;
}
void copy_tables(u8 rounds) ABI {
    u16 i;
    while(rounds--) {
    for(i=0;i<1024;++i)buffer[i]=0;
    drain();
    for(i=0;i<1024;++i)buffer[i]=first[i];
    drain();
    for(i=0;i<1024;++i)buffer[i]=second[i];
    drain();
    }
}
u16 sample(u16 i) ABI {return words[i&63]+words[(i+1)&63];}
'''
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env.update(SDCC_HOME=str(a.support.resolve()), MX251_WIDE_VALUE_LIMIT='1024', MX251_WIDE_NODE_LIMIT='2048',
           MX251_BACKEND_CANDIDATES='1')
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        for abi in (2, 4):
            variants = {}
            for name, compiler in [('before', a.baseline), ('after', a.sdcc)]:
                root = a.output/f'{model}-{opt}-abi{abi}'/name
                root.mkdir(parents=True)
                (root/'test.c').write_text(source, newline='\n')
                cmd = [str(compiler.resolve()), '-mmcs251', '--model-'+model, '--stack-auto',
                       '--opt-code-'+opt, '--mcs251-wide-ordinary', '--mcs251-native-abi',
                       '--mcs251-machine-ir', '--mcs251-flat-code-reads',
                       '--mcs251-function-metadata', '-DTEST_ABI='+str(abi),
                       '-c', 'test.c']
                q = subprocess.run(cmd, cwd=root, env=env, capture_output=True, timeout=180)
                (root/'compiler.log').write_bytes(q.stdout+q.stderr)
                assert q.returncode == 0, (root, q.stderr)
                asm = (root/'test.asm').read_text(encoding='latin1')
                size = sum(int(n,16) for n in re.findall(r'^A CSEG(?:\$\S+)? size ([0-9A-F]+)',
                                                       (root/'test.rel').read_text(), re.M))
                variants[name] = dict(asm=asm, bytes=size, diagnostics=asm+q.stderr.decode('latin1'))
            new = variants['after']['asm']
            assert variants['after']['bytes'] <= variants['before']['bytes'], (model,opt,abi,variants)
            choices = []
            for fn,selected,wide,legacy,unknown_w,unknown_l in re.findall(
                r'mx251-backend-choice function=(\S+) selected=(\S+) wide-bytes=(\d+) legacy-bytes=(\d+)[^\n]*?unknown=(\d+)/(\d+)', new):
                if f'mx251-body-choice function={fn} ' not in new or int(unknown_w) or int(unknown_l):
                    continue
                wide,legacy = int(wide),int(legacy)
                assert (wide if selected=='wide' else legacy) == min(wide,legacy), (fn,selected,wide,legacy)
                choices.append(dict(function=fn,selected=selected,wide=wide,legacy=legacy))
            assert choices and any('scan' in c['function'] for c in choices), (model,opt,abi)
            evidence=variants['after']['diagnostics']
            assert 'wide symbolic-code-index choice' in evidence and 'wide symbolic-index choice' in evidence
            rows.append(dict(model=model,optimization=opt,abi=abi,
                             before=variants['before']['bytes'],after=variants['after']['bytes'],choices=choices))
result = dict(status='PASS',configurations=rows,hardwareExecuted=False,
              compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
              baselineSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest())
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',newline='\n')
print('PASS: 8 model/optimization/ABI modes; complete-body minimum sizes and mixed-memory loops')
