"""Execute emitted caller code with independent argument and clobber models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'parent', 'support', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
header = '''typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI(n) __reentrant __sdcccall(n)
volatile __edata u8 input0,input1,input2,input3,output;
void sink8(u8,u8) ABI(4);void sink16(u16,u16) ABI(4);
void sink24(u8 __xdata *,u8 __xdata *) ABI(4);void sink32(u32,u32) ABI(4);
void sink_stack(u8,u8,u16) ABI(4);void sink_clobber(u8,u8) ABI(4);
void old_sink(u8,u8) ABI(2);void unknown(u8,u8) ABI(4);
void (*callback)(u8,u8) ABI(4);
'''
calls = dict(byte='sink8(0x12,0x34);', word='sink16(0x1234,0x5678);',
    pointer='sink24((u8 __xdata *)0x123456UL,(u8 __xdata *)0x234567UL);',
    dword='sink32(0x12345678UL,0x90abcdefUL);', stack='sink_stack(0x12,0x34,0x5678);',
    clobber='sink_clobber(0x12,0x34);', old='old_sink(0x12,0x34);',
    registers='sink8(w,x);', memory='sink8(input0,input1);',
    repeated='sink8(0x12,0x34);sink8(0x12,0x34);')
setup='u8 w=input0,x=input1,y=input2,z=input3;'
finish='output=w^x^y^z;'
source=header+''.join('void test_'+name+'(void) ABI(CALLER) {'+setup+call+finish+'}\n' for name,call in calls.items())
source+='void irq(void) __interrupt(1) {'+setup+calls['byte']+finish+'}\n'
source+='void unknown_call(void) ABI(CALLER) {'+setup+'unknown(0x12,0x34);'+finish+'}\n'
source+='void indirect_call(void) ABI(CALLER) {'+setup+'callback(0x12,0x34);'+finish+'}\n'
source+='void other_bank(void) __reentrant __using(1) {'+setup+calls['byte']+finish+'}\n'
effects='MX251-CALL-CLOBBERS-4\n'+''.join(
    f'global ___m251r4_sink{suffix} 4 {mask:08x} 0 0\n'
    for suffix,mask in [('8',0),('16',0),('24',0),('32',0),('_stack',0),('_clobber',0x10)])
effects+='global _old_sink 2 00000000 0 0\n'


def body(text, name):
    return re.split(r'; mx251-function begin schema=1 symbol=(?:_|___m251r4_)'+name+' ', text, maxsplit=1)[1].split('; mx251-function end', 1)[0]


def instructions(text):
    return [line for raw in text.splitlines() if (line:=raw.split(';',1)[0].strip())
            and not line.endswith(':') and not re.search(r'\s=\s',line)
            and not line.startswith(('isr=', '.'))]


def execute(text, case, seed):
    """Small straight-line ISA model; callee effects are explicit test facts."""
    rng=random.Random(seed);regs=bytearray(rng.randrange(256) for _ in range(32))
    inputs=[rng.randrange(256) for _ in range(4)]
    cells={f'_input{i}':v for i,v in enumerate(inputs)};cells['_output']=0
    special={'dpl':0,'dph':0,'dpxl':0,'psw':0,'0xd1':0};mem={};sp=0x1000;calls_seen=[]
    def reg(s):
        s={'a':'r11','acc':'r11','b':'r10'}.get(s,s)
        if s.startswith('ar'):s=s[1:]
        m=re.fullmatch(r'(w|d)?r(\d+)',s)
        return (int(m[2]),{None:1,'w':2,'d':4}[m[1]]) if m else None
    def width(s):return reg(s)[1] if reg(s) else 4 if s=='spx' else 1
    def direct(s):
        m=re.fullmatch(r'e:\((\w+)\+0\)',s)
        return m[1] if m else s
    def read(s,n=None):
        if s.startswith('#'):return int(s[1:],0)
        if s=='spx':return sp
        if s.startswith('@spx'):
            at=sp+(int(s[4:],0) if s[4:] else 0)
            return int.from_bytes(bytes(mem[at+i] for i in range(n)), 'big')
        r=reg(s)
        if r:return int.from_bytes(regs[r[0]:r[0]+r[1]],'big')
        key=direct(s)
        return cells[key] if key in cells else special[key]
    def write(s,v,n=None):
        nonlocal sp
        if s=='spx':sp=v;return
        if s.startswith('@spx'):
            at=sp+(int(s[4:],0) if s[4:] else 0)
            for i,b in enumerate((v&((1<<(n*8))-1)).to_bytes(n,'big')):mem[at+i]=b
            return
        r=reg(s)
        if r:regs[r[0]:r[0]+r[1]]=(v&((1<<(r[1]*8))-1)).to_bytes(r[1],'big');return
        key=direct(s)
        if key in cells:cells[key]=v&255
        else:special[key]=v&255
    for line in instructions(text):
        op,*tail=line.split(None,1);args=[x.strip() for x in tail[0].split(',')] if tail else []
        if op in ('eret','ret','reti'):break
        if op in ('push','pushw'):
            n=2 if op=='pushw' else width(args[0]);v=read(args[0])
            for b in (v&((1<<(n*8))-1)).to_bytes(n,'big'):sp+=1;mem[sp]=b
        elif op=='pop':
            n=width(args[0]);write(args[0],int.from_bytes(bytes(mem[sp-n+1+i] for i in range(n)),'big'));sp-=n
        elif op=='mov':
            n=width(args[1]) if args[0].startswith('@') else width(args[0]);write(args[0],read(args[1],n),n)
        elif op=='clr':write(args[0],0)
        elif op in ('inc','dec'):
            write(args[0],read(args[0])+(1 if op=='inc' else -1)*(read(args[1]) if len(args)>1 else 1))
        elif op=='xrl':write(args[0],read(args[0])^read(args[1]))
        elif op in ('ecall','lcall','acall'):
            target=args[0];suffix=target.removeprefix('___m251r4_sink')
            if target=='_old_sink':values=[read('dpl'),mem[sp]]
            else:
                n={'8':1,'16':2,'24':3,'32':4,'_stack':1,'_clobber':1}[suffix]
                values=[int.from_bytes(regs[8-n:8],'big'),int.from_bytes(regs[16-n:16],'big')]
                if suffix=='_stack':values.append(int.from_bytes(bytes([mem[sp-1],mem[sp]]),'big'))
            expected=inputs[:2] if case in ('registers','memory') else {
                'word':[0x1234,0x5678],'pointer':[0x123456,0x234567],
                'dword':[0x12345678,0x90abcdef],'stack':[0x12,0x34,0x5678]}.get(case,[0x12,0x34])
            assert values==expected,(case,seed,'arguments',values,expected)
            calls_seen.append(target)
            if suffix=='_clobber':regs[4]=0xe3
        else:raise AssertionError((case,line))
    assert cells['_output']==inputs[0]^inputs[1]^inputs[2]^inputs[3],(case,seed,'live values',cells,inputs)
    assert sp==0x1000,(case,seed,'stack',sp)
    assert len(calls_seen)==(2 if case=='repeated' else 1),(case,calls_seen)


rows=[]
for model in ('small','large'):
    for opt in ('size','speed'):
        for caller in (2,4):
            d=a.output/f'{model}-{opt}-abi{caller}';d.mkdir();texts={}
            for label,compiler in (('new',a.sdcc),('parent',a.parent)):
                out=d/label;out.mkdir();(out/'fixture.c').write_text(source);(out/'effects.txt').write_text(effects)
                command=[str(compiler),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto',
                    '--mcs251-native-abi','--mcs251-function-metadata','--mcs251-leaf-effects','effects.txt',
                    '-DCALLER='+str(caller),'-c','fixture.c']
                q=subprocess.run(command,cwd=out,env=env,capture_output=True)
                (out/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,(out,q.stderr)
                texts[label]=(out/'fixture.asm').read_text()
            for case in calls:
                for label in texts:
                    for seed in range(64):execute(body(texts[label],'test_'+case),case,seed)
            for name in ('unknown_call','indirect_call','other_bank'):
                assert instructions(body(texts['new'],name))==instructions(body(texts['parent'],name)),(d,name)
            for case in ('byte','clobber','registers','memory','repeated')+ (('word','pointer') if caller==2 else ()):
                b=body(texts['new'],'test_'+case)
                assert 'mx251 call-clobber save' in b,(d,case,b)
                for removed,writes,arguments in re.findall(r'removed=([0-9a-f]+) writes=([0-9a-f]+) arguments=([0-9a-f]+)',b):
                    assert not (int(removed,16)&(int(writes,16)|int(arguments,16))),(d,case)
            irq=body(texts['new'],'irq');assert 'mx251 call-clobber save' in irq,(d,irq)
            for seed in range(64):execute(irq,'byte',seed)
            rows.append(dict(model=model,optimization=opt,callerAbi=caller,executions=21*64))
(a.output/'results.json').write_text(json.dumps(dict(status='PASS',rows=rows,
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),executions=sum(x['executions'] for x in rows),
    hardwareExecuted=False),indent=2)+'\n')
print('PASS',len(rows),'configurations;',sum(x['executions'] for x in rows),'modeled executions; ABI arguments/live values/stack/ISR')
