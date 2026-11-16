"""Bit transaction chains, ISR memory provenance and binary32 helper admission."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'toolchain', 'output'):
    p.add_argument('--'+key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env.update(SDCC_HOME=str(a.toolchain.resolve()), MX251_BACKEND_CANDIDATES='1',
           MX251_WIDE_VALUE_LIMIT='1024', MX251_WIDE_NODE_LIMIT='2048')
env['PATH'] = str(a.toolchain.resolve()/'bin')+os.pathsep+env['PATH']
rows = []
cases = {
    'pointer-volatile': 'const __code unsigned int table[2]={1,2}; struct record {unsigned char before; const unsigned int *cursor; unsigned char after;}; volatile __edata struct record record; void test(void) {record.cursor=table;}',
    'pointer-self': 'void * __xdata slot; void test(void) {void **p=&slot; *p=p;}',
    'toggle': 'volatile __bit bit; void test(void) {bit=!bit;}',
    'used-result': 'volatile __bit bit; unsigned char test(void) {unsigned char x=!bit; bit=x; return x;}',
    'other-bit': 'volatile __bit bit,other; void test(void) {other=!bit;}',
    'intervening-write': 'volatile __bit bit; volatile unsigned char observed; void test(void) {unsigned char x=!bit; observed=x; bit=x;}',
    'known-store': 'volatile __data unsigned char value; void test(void) __interrupt(1) {*(volatile __xdata unsigned char *)0x12000UL=value;}',
    'unknown-store': 'volatile __data unsigned char value; volatile __xdata unsigned char *pointer; void test(void) __interrupt(1) {*pointer=value;}',
    'core-store': 'volatile __data unsigned char value; void test(void) __interrupt(1) {*(volatile __xdata unsigned char *)0xe0UL=value;}',
    'wrapped-store': 'volatile __data unsigned int value; void test(void) __interrupt(1) {*(volatile __xdata unsigned int *)0xffffffUL=value;}',
    'float-helpers': 'float test(long x,float y) {return (float)x*y+3.0f;} long convert(float x) {return (long)x;} unsigned char compare(float x,float y) {return x<y;}',
}
for model in ('small', 'large'):
    for name, source in cases.items():
        root = a.output/(model+'-'+name); root.mkdir()
        (root/'test.c').write_text(source, newline='\n')
        cmd = [str(a.sdcc.resolve()), '-mmcs251', '--model-'+model, '--stack-auto',
               '--opt-code-speed', '--mcs251-wide-ordinary', '--mcs251-machine-ir', '-c', 'test.c']
        q = subprocess.run(cmd, cwd=root, env=env, capture_output=True)
        (root/'compiler.log').write_bytes(q.stdout+q.stderr)
        assert q.returncode == 0, (name, q.stderr)
        asm = (root/'test.asm').read_text(encoding='latin1')
        candidates = q.stderr.decode('latin1')
        text = candidates or asm
        if name == 'toggle':
            assert 'wide bit update inverse=1 exact-read=1 exact-write=1' in text
            assert re.search(r'mov\s+c,_bit\s*\n\s*cpl\s+c\s*\n\s*mov\s+_bit,c', text)
        elif name in ('used-result', 'other-bit', 'intervening-write'):
            assert 'wide bit update' not in text, name
        elif name.startswith('pointer-'):
            assert 'wide symbolic pointer store' in text, name
        elif name.endswith('store'):
            if 'kind=wide begin' in candidates:
                text = candidates.split('kind=wide begin', 1)[1].split('kind=legacy begin', 1)[0]
            if name == 'known-store':
                assert 'mx251-leaf-isr-saves' in text and 'mx251-memory-origin begin' in text
            else:
                assert 'mx251-leaf-isr-saves' not in text, name
        else:
            assert 'call-abi-or-argument-contract' not in asm, (model, name)
            assert all('accepted: '+fn+';' in asm or 'function=_'+fn+' selected=' in asm for fn in ('test', 'convert', 'compare'))
        rows.append(dict(model=model,case=name,status='PASS'))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',rows=rows,hardwareExecuted=False),indent=2)+'\n',newline='\n')
print('PASS',len(rows),'compiler cases')
