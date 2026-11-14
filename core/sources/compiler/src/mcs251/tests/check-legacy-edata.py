"""Legacy EDATA byte transactions, endian order, and unchanged far/code paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'parent', 'support', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = dict(os.environ, SDCC_HOME=str(a.support.resolve()))
env['PATH'] = str(a.support.resolve()/'bin')+os.pathsep+env['PATH']
source = '''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
volatile __edata u8 byte;
volatile __edata u16 word;
volatile __edata u32 dword;
volatile u16 default_cell;
volatile __xdata u16 farcell;
static volatile __xdata u16 initialized=0x5789;
volatile const __code u16 rom=0x3579;
volatile __edata __at(0xfffe) u16 edge;
volatile __edata __at(0xffff) u16 crossing;
volatile __edata __at(0x10) u8 low_absolute;
struct State {u8 tag;u16 count;u8 bytes[3];};
volatile __edata struct State state;
u32 read_edata(void) __reentrant __sdcccall(2) {return dword;}
void write_edata(u32 x) __reentrant __sdcccall(4) {dword=x;}
void transactions(void) __reentrant {byte;word;word;dword;word=0x1234;}
u16 read_far(void) __reentrant {return farcell;}
u16 read_initialized(void) __reentrant {return initialized;}
u16 read_code(void) __reentrant {return rom;}
void write_far(u16 x) __reentrant {farcell=x;}
u16 read_edge(void) __reentrant {return edge;}
u16 read_crossing(void) __reentrant {return crossing;}
void worker(void) __interrupt(1) {word+=0x1234;default_cell++;state.count=word;state.bytes[1]=byte;}
void fixed_worker(void) __interrupt(2) {edge++;}
void core_alias(void) __interrupt(3) {low_absolute++;}
'''


def body(text, name):
    return re.split(r'; mx251-function begin schema=1 symbol=(?:_|___m251r4_)'+name+' ', text, maxsplit=1)[1].split('; mx251-function end', 1)[0]


rows = []
for model in ('small', 'large'):
    for opt in ('size', 'speed'):
        for stack in (False, True):
            d = a.output/f'{model}-{opt}-{int(stack)}';d.mkdir()
            texts = {}
            for label, compiler in (('new', a.sdcc), ('parent', a.parent)):
                out = d/label;out.mkdir()
                default_edata = model == 'small' and stack
                (out/'fixture.c').write_text(source if default_edata else
                    source.replace('volatile u16 default_cell;', 'volatile __edata u16 default_cell;'))
                command = [str(compiler), '-mmcs251', '--model-'+model, '--opt-code-'+opt,
                           '--std-sdcc11', '--mcs251-native-abi',
                           '--mcs251-function-metadata', '-c', 'fixture.c']
                if default_edata: command.insert(1, '--mcs251-default-edata')
                if stack: command.insert(1, '--stack-auto')
                run = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True)
                (out/'compile.log').write_text(run.stdout+run.stderr)
                assert run.returncode == 0, (out, run.stderr)
                texts[label] = (out/'fixture.asm').read_text()
            text = texts['new']
            b = body(text, 'transactions')
            accesses = re.findall(r'\bmov\s+(?:a,e:\((\w+)\+(\d+)\)|e:\((\w+)\+(\d+)\),a)', b)
            expected = [(n, str(i), '', '') for n, offsets in
                        [('_byte', [0]), ('_word', [1, 0, 1, 0]), ('_dword', [3, 2, 1, 0])]
                        for i in offsets]+[('', '', '_word', '1'), ('', '', '_word', '0')]
            assert accesses == expected, (d, accesses)
            for name in ('read_edata', 'write_edata', 'transactions', 'read_edge'):
                b = body(text, name)
                assert 'e:(' in b and not re.search(r'\b(?:dptr|dpxl|@dpx)\b', b), (d, name, b)
            for name in ('read_far', 'read_initialized', 'read_code', 'write_far', 'read_crossing'):
                assert body(text, name) == body(texts['parent'], name), (d, name)
            b = body(text, 'worker')
            assert 'e:(_default_cell+' in b and 'e:(_word+' in b, (d, b)
            assert not re.search(r'mov\s+dptr,#\(?_(?:word|default_cell)\b', b), d
            assert 'mx251-leaf-isr-saves' in b and 'leaf-isr-fallback' not in b, (d, b)
            assert 'mx251-leaf-isr-saves' in body(text, 'fixed_worker'), d
            assert 'leaf-isr-fallback' in body(text, 'core_alias'), d
            rows.append(dict(model=model, optimization=opt, stackAuto=stack,
                             volatilePhysicalOrder=True, farAndCodeIdentical=True))
(a.output/'results.json').write_text(json.dumps(dict(status='PASS', rows=rows,
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest()), indent=2)+'\n')
print('PASS', len(rows), 'legacy configurations; EDATA order, ISR, far/code boundaries')
