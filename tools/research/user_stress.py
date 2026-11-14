"""Synthetic user-code stress corpus, equivalent forms, and backend refusal census."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import machine_pipeline_fixture as board

CASES = ('fields', 'parser', 'ring', 'bits', 'callback', 'giant')
SOURCES = board.REPO/'core/sources/compiler/src/mcs251/tests/user-stress'


def generate(root):
    root.mkdir(parents=True, exist_ok=False)
    for p in SOURCES.iterdir():
        shutil.copy2(p, root/p.name)
    # Deliberately unroll a small loop into a maintenance-style copy/paste giant.
    # Identical unsigned arithmetic, no undefined shifts or signed overflow.
    step = 'x = (x << 3) ^ (x >> 7) ^ (0x9e370001UL + (u32)(K) * 257UL); y += x ^ (u32)(K);'
    text = '#include "stress.h"\n#define STEP(K) do { '+step+' } while (0)\n'
    text += 'u32 giant_clean(u8 seed) { u8 i; u32 x=seed, y=0x87654321UL;\n'
    text += 'for(i=0;i<96;++i) STEP(i); return x^y; }\n'
    text += 'u32 giant_messy(u8 seed) { u32 x=seed, y=0x87654321UL;\n'
    text += '\n'.join(f'STEP({i});' for i in range(96))+'\nreturn x^y; }\n'
    (root/'giant.c').write_text(text)
    board.write(root/'source.json', dict(generatorSha256=board.sha(Path(__file__)),
        files={p.name:board.sha(p) for p in sorted(root.iterdir()) if p.suffix in ('.c','.h')}))


def run(command, cwd, env=None, label='run'):
    r = subprocess.run([str(x) for x in command], cwd=cwd, env=env, capture_output=True)
    (cwd/(label+'.log')).write_bytes(r.stdout+r.stderr)
    assert r.returncode == 0, (cwd, label, r.stderr.decode(errors='replace')[-2000:])
    return r.stdout.decode()


def reference(root, gcc):
    host = root/'host'
    generate(host)
    (host/'main.c').write_text('''#include <stdio.h>
#include "stress.h"
int main(void) {
    unsigned seed, kind;
    for(seed=0;seed<256;++seed) for(kind=0;kind<12;++kind)
        printf("%u %u %lu\\n",kind,seed,(unsigned long)stress_run(kind,seed));
    return 0;
}
''')
    run([gcc, '-std=c99', '-O2', '-Wall', *sorted(p.name for p in host.glob('*.c')), '-o', 'reference.exe'], host, label='compile')
    values = {f'{k}:{s}':int(v) for k,s,v in (line.split() for line in run([host/'reference.exe'],host).splitlines())}
    for seed in range(256):
        for kind in range(0,12,2):
            assert values[f'{kind}:{seed}'] == values[f'{kind+1}:{seed}'], (kind,seed)
    board.write(root/'reference.json', values)
    board.write(root/'host-result.json',dict(status='PASS',evaluations=len(values),equivalentPairs=6*256,
        compiler=str(gcc),unsignedWidths=[8,16,32],seedRange=[0,255]))
    return values


def measure(root, core):
    source = root/'source'
    generate(source)
    policy = json.loads((core/'build/qualified/TargetContracts.json').read_text('utf-8'))
    targets = list(policy['targets']) + ['Independent251']
    shared = policy['architectures']['mcs251-flat24-codeff']['compilerCapabilities']
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
    env['SDCC_HOME'] = str(core/'toolchain')
    env['PATH'] = str(core/'toolchain/bin')+';'+env['PATH']
    rows = []
    for model, opt in [('small','size'), ('small','speed'), ('large','size')]:
        for target in targets:
            effects = policy['targets'].get(target,{}).get('deviceEffects',{})
            bits = effects.get('orderedBits',{})
            flags = ['-mmcs251', '--model-'+model, '--opt-code-'+opt, '--std-sdcc11', '--stack-auto',
                     '--mcs251-wide-ordinary', '--mcs251-machine-ir', '--mcs251-function-metadata']
            if 'nativeCodeReads' in shared: flags += ['--mcs251-flat-code-reads']
            if 'orderedAsm' in shared: flags += ['--mcs251-ordered-asm']
            if 'sfrLayout' in effects: flags += ['--mcs251-sfr-layout', effects['sfrLayout']['contract']]
            if bits.get('sfrBitMasks'): flags += ['--mcs251-ordered-asm-bits',bits['sfrBitMasks']]
            if bits.get('memoryStateBits'): flags += ['--mcs251-memory-state-bits',bits['memoryStateBits']]
            out = root/(target+'-'+model+'-'+opt)
            out.mkdir()
            shutil.copy2(source/'stress.h',out/'stress.h')
            for p in sorted(source.glob('*.c')):
                shutil.copy2(p,out/p.name)
                run([core/'toolchain/bin/sdcc.exe',*flags,'-c',p.name],out,env,p.stem)
                asm = (out/(p.stem+'.asm')).read_text(errors='replace')
                rows.append(dict(target=target,model=model,optimization=opt,unit=p.stem,
                    codeBytes=int(re.search(r'^A CSEG size ([0-9A-Fa-f]+)',(out/(p.stem+'.rel')).read_text(),re.M)[1],16),
                    refusals=re.findall(r'^; wide refusal function=(\S+) stage=\S+ reason=(\S+)',asm,re.M),
                    admissions=re.findall(r'^; wide admission function=(\S+) skip=(\S+)',asm,re.M),
                    backendChoices=re.findall(r'^; mx251-backend-choice function=_(\S+) selected=(\S+)',asm,re.M)))
    # Names/peripheral descriptions must not change pure-C machine code.
    # Ignore assembler headers; REL content contains code/relocations and symbols.
    for model,opt in [('small','size'),('small','speed'),('large','size')]:
        for p in sorted(source.glob('*.c')):
            objects = [(root/(t+'-'+model+'-'+opt)/(p.stem+'.rel')).read_bytes() for t in targets]
            assert all(b == objects[0] for b in objects[1:]), (model,opt,p.name,'target-dependent pure C')
    board.write(root/'compilation.json', dict(status='PASS',compilerSha256=board.sha(core/'toolchain/bin/sdcc.exe'),
        configurations=9, translationUnits=len(rows), targets=targets, identicalPureCObjects=True,
        valueLimit=128,nodeLimit=512,source=board.sha(source/'source.json'),rows=rows))
    print('PASS',len(rows),'translation units; pure C objects identical across 3 target descriptions',flush=True)


def create_board(output):
    generate(output)
    source = (SOURCES.parent/'machine-board.c').read_text().replace('"machine-board.h"','"stress.h"')
    start,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:start]+source[end:]
    start,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:start]+'        result = stress_run(kind,input);\n'+source[end:]
    source=source.replace('kind < 2','kind < 12')
    source=source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (output/'board.c').write_text(source)
    files=sorted(p.name for p in output.iterdir() if p.suffix in ('.c','.h'))
    units=[dict(name='u'+str(i),file=n,backend='ordinary-wide',stackAuto=True,defines=[])
           for i,n in enumerate(files) if n.endswith('.c')]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(user_stress NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')


def capture(hexfile, output, reference_path):
    values=json.loads(reference_path.read_text())
    def check(data):
        seen={}; frames=0; overflow=0; mismatches=[]
        for i in range(len(data)-15):
            if data[i:i+4]!=b'MIR1': continue
            frame=data[i:i+16]; checksum=0
            for b in frame[:14]: checksum^=b
            if checksum or frame[-2:]!=b'\r\n': continue
            kind,seed=frame[4:6]; assert kind<12 and seed<64
            expected=values[f'{kind}:{seed}']
            actual=int.from_bytes(frame[6:10],'big')
            if actual!=expected and f'{kind}:{seed}' not in seen:
                mismatches.append(dict(case=CASES[kind//2],variant='messy' if kind&1 else 'clean',
                                       kind=kind,seed=seed,actual=actual,expected=expected))
            seen[f'{kind}:{seed}']=actual; frames+=1; overflow+=bool(frame[12])
        return dict(status='PASS' if len(seen)==768 and not mismatches else 'FAIL',cases=len(seen),
                    expectedCases=768,frames=frames,timerOverflowFrames=overflow,
                    timingQualified=False,mismatches=mismatches,values=seen)
    board.capture(hexfile,output,check,max_frames=768*2)
    result=json.loads((output/'result.json').read_text())
    assert result['status']=='PASS',(result['cases'],len(result['mismatches']))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('measure')
    for n in ('output','core','gcc'): c.add_argument('--'+n,type=Path,required=True)
    c=sub.add_parser('board');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture')
    for n in ('hex','output','reference'):c.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    if a.command=='measure':
        a.output.mkdir(parents=True,exist_ok=False)
        reference(a.output,a.gcc);measure(a.output,a.core)
    elif a.command=='board':create_board(a.output)
    else:capture(a.hex,a.output,a.reference)
