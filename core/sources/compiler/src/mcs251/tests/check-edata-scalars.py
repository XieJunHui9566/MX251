"""EDATA scalar snapshots, native dir16 relocation and volatile byte order."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

spec = importlib.util.spec_from_file_location('arguments', Path(__file__).with_name('check-argument-object-loads.py'))
arguments = importlib.util.module_from_spec(spec)
spec.loader.exec_module(arguments)
SOURCE = arguments.SOURCE.replace('__data', '__edata') + r'''
__edata __at(0x7fc) u32 absolute;
__edata __at(0x80) unsigned char low_absolute;
u32 volatile_native(void) __sdcccall(4) { return take_native(device_dword,device_word,device_dword); }
u32 read_volatile(void) __sdcccall(4) { return device_dword; }
void write_volatile(u32 x) __sdcccall(4) { device_dword=x; }
void constant_volatile(void) __sdcccall(4) { device_dword=0x12345678UL; }
void copy_volatile(void) __sdcccall(4) { device_word=(u16)device_dword; }
u32 two_volatile(void) __sdcccall(4) { return device_dword+device_word; }
u32 normal_absolute(void) __sdcccall(4) { absolute=dword; return absolute; }
unsigned char normal_low(void) __sdcccall(4) { low_absolute=0x5a; return low_absolute; }
u32 alias_write(u32 __edata *p,u32 x) __sdcccall(4) { dword=x; *p=~x; return dword; }
'''


def accesses(text):
    # Legacy linear symbols materialize DPTR before MOV @DPX; native uses E:.
    result = []
    for line in text.splitlines():
        if not line.startswith('\tmov\t'):
            continue
        match = re.search(r'(?:dptr,#|e:)(?:\()?_(device_word|device_dword)(?:\s*\+\s*(\d+))?', line)
        if match:
            result.append((match[1], int(match[2] or 0)))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdcc', 'baseline', 'support', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
    env['SDCC_HOME'] = str(a.support.resolve())
    env['PATH'] = str(a.support.resolve()/'bin')+';'+env['PATH']
    rows=[]
    for model, opt in [('small','speed'), ('large','size')]:
        out=a.output/(model+'-'+opt);out.mkdir()
        (out/'probe.c').write_text(SOURCE)
        assemblies={}
        for tag, cc in [('parent',a.baseline),('candidate',a.sdcc)]:
            command=[str(cc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,
                     '--stack-auto','--mcs251-wide-ordinary','--mcs251-native-abi',
                     '--mcs251-machine-ir','--mcs251-function-metadata','-c','probe.c','-o',tag+'.rel']
            run=subprocess.run(command,cwd=out,env=env,capture_output=True,timeout=90)
            (out/(tag+'.log')).write_bytes(run.stdout+run.stderr)
            assert run.returncode==0,run.stderr.decode(errors='replace')
            assemblies[tag]=(out/(tag+'.asm')).read_text()
        functions=re.findall(r'\b(\w+)\(.*?\) __sdcccall\(4\) \{',SOURCE)
        for name in functions:
            if name=='sfr_argument':continue
            text=arguments.body(assemblies['candidate'],name)
            assert 'wide-mir accepted: '+name+';' in text,(name,text)
        for name in ['volatile_word','volatile_dword','volatile_stack','volatile_native',
                     'read_volatile','write_volatile','constant_volatile','copy_volatile','two_volatile']:
            old=accesses(arguments.body(assemblies['parent'],name))
            new=accesses(arguments.body(assemblies['candidate'],name))
            assert old and old==new,(name,old,new)
        for name,symbol,width in [('word','word','wr'),('dword','dword','dr'),
                                  ('float','real','dr'),('external','external','dr')]:
            text=arguments.body(assemblies['candidate'],'normal_'+name)
            assert re.search(r'\tmov\t'+width+r'\d+,e:\(_'+symbol+r'\+0\)',text),text
        # DATA/SFR access must retain its separate address space.
        assert arguments.instructions(arguments.body(assemblies['parent'],'sfr_argument'))==arguments.instructions(arguments.body(assemblies['candidate'],'sfr_argument'))
        sizes={tag:int(re.search(r'^A CSEG size ([0-9A-Fa-f]+)',(out/(tag+'.rel')).read_text(),re.M)[1],16) for tag in assemblies}
        assert sizes['candidate']<sizes['parent'],sizes
        rows.append(dict(model=model,objective=opt,sizes=sizes,acceptedFunctions=len(functions)-1,volatileTraces=9))
    result=dict(status='PASS',rows=rows,compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest())
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
