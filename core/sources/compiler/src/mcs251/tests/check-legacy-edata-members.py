"""Fixed EDATA member addressing: transactions, aliases and address spaces."""
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
struct State {u8 tag;u16 count;u32 total;u8 bytes[5];};
volatile __edata struct State state;
struct Slot {u8 tag;u8 *value;};
volatile __edata struct Slot slot;
volatile __edata u16 sink;
volatile __xdata struct State farstate;
volatile const __code struct State rom={1,2,3,{4,5,6,7,8}};
volatile __edata __at(0xfff4) struct State edge;
volatile __edata __at(0xfff5) struct State crossing;
volatile __edata __at(0x08) struct State low;
volatile struct State * volatile saved;
struct Bits {unsigned int bits:3;unsigned int rest:5;};
volatile __edata struct Bits bits;
u16 read_member(void) __reentrant __sdcccall(2) {return state.count;}
u32 read_long(void) __reentrant __sdcccall(4) {return state.total;}
void write_member(u32 x) __reentrant __sdcccall(4) {state.total=x;}
u8 *read_pointer(void) __reentrant __sdcccall(4) {return slot.value;}
void write_pointer(u8 *p) __reentrant __sdcccall(4) {slot.value=p;}
void repeated(void) __reentrant {state.count;state.count;state.total=0x12345678UL;}
u16 local_alias(void) __reentrant {volatile struct State *p=&state;return p->count;}
u8 cast_bytes(void) __reentrant {return ((volatile u8 *)&state)[6];}
u8 subtract(void) __reentrant {volatile u8 *p=&state.bytes[4];return p[-2];}
u8 condition(void) __reentrant {if(state.tag)return 7;return 9;}
void from_far(void) __reentrant {state.count=farstate.count;}
void to_far(void) __reentrant {farstate.count=state.count;}
void alias_write(void) __reentrant {saved->count=0x1234;sink=state.count;}
u8 dynamic(u8 i) __reentrant {return state.bytes[i];}
u16 unknown(volatile struct State *p) __reentrant {return p->count;}
u16 volatile_pointer(void) __reentrant {return saved->count;}
u16 read_far(void) __reentrant {return farstate.count;}
u16 read_code(void) __reentrant {return rom.count;}
u16 read_edge(void) __reentrant {return edge.count;}
u16 read_crossing(void) __reentrant {return crossing.count;}
u8 read_bits(void) __reentrant {return bits.bits;}
u8 postincrement(volatile u8 **p) __reentrant {return *(*p)++;}
u8 narrow_cast(void) __reentrant {return *(volatile u8 *)(u16)&state;}
void worker(void) __interrupt(1) {state.count++;state.bytes[2]=state.tag;}
void core_alias(void) __interrupt(2) {low.count++;}
'''


def body(text, name):
    return re.split(r'; mx251-function begin schema=1 symbol=(?:_|___m251r4_)'+name+' ', text, maxsplit=1)[1].split('; mx251-function end', 1)[0]


def transactions(text, symbol='_state'):
    result = []
    for line in text.splitlines():
        m = re.fullmatch(r'\s*mov\s+(a,e:\('+symbol+r'\+(\d+)\)|e:\('+symbol+r'\+(\d+)\),a)\s*', line)
        if m: result.append(('read' if m[2] else 'write', int(m[2] or m[3])))
    return result


rows = []
for model in ('small', 'large'):
    for opt in ('size', 'speed'):
        for stack in (False, True):
            d = a.output/f'{model}-{opt}-{int(stack)}'; d.mkdir()
            texts = {}
            for label, compiler in (('new', a.sdcc), ('parent', a.parent)):
                out = d/label; out.mkdir(); (out/'fixture.c').write_text(source)
                command = [str(compiler), '-mmcs251', '--model-'+model, '--opt-code-'+opt,
                           '--std-sdcc11', '--mcs251-native-abi', '--mcs251-function-metadata', '-c', 'fixture.c']
                if stack: command.insert(1, '--stack-auto')
                run = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True)
                (out/'compile.log').write_text(run.stdout+run.stderr)
                assert run.returncode == 0, (out, run.stderr)
                texts[label] = (out/'fixture.asm').read_text()
            text = texts['new']
            expected = dict(read_member=[('read', 1), ('read', 2)],
                local_alias=[('read', 1), ('read', 2)],
                read_long=[('read', i) for i in range(3, 7)],
                write_member=[('write', i) for i in range(3, 7)],
                repeated=[('read', i) for i in (1, 2, 1, 2)]+[('write', i) for i in range(3, 7)],
                cast_bytes=[('read', 6)], subtract=[('read', 9)], condition=[('read', 0)])
            for name, accesses in expected.items():
                b = body(text, name)
                assert transactions(b) == accesses, (d, name, transactions(b), b)
                assert not re.search(r'\b(?:dptr|dpxl),#|@dpx', b), (d, name, b)
            for name in ('dynamic', 'unknown', 'volatile_pointer', 'read_far', 'read_code',
                         'read_crossing', 'read_bits', 'postincrement', 'narrow_cast'):
                assert body(text, name) == body(texts['parent'], name), (d, name)
            for name, kind in (('read_pointer', 'read'), ('write_pointer', 'write')):
                b = body(text, name)
                assert transactions(b, '_slot') == [(kind, i) for i in (1, 2, 3)], (d, name, b)
                assert not re.search(r'\b(?:dptr|dpxl),#|@dpx', b), (d, name, b)
            assert transactions(body(text, 'read_edge'), '_edge') == [('read', 1), ('read', 2)], d
            assert transactions(body(text, 'from_far')) == [('write', 1), ('write', 2)], d
            assert transactions(body(text, 'to_far')) == [('read', 1), ('read', 2)], d
            b = body(text, 'alias_write')
            assert transactions(b) == [('read', 1), ('read', 2)] and ('@dpx' in b or '__gptrput' in b), (d, b)
            b = body(text, 'worker')
            assert 'mx251-leaf-isr-saves' in b and 'leaf-isr-fallback' not in b, (d, b)
            assert 'leaf-isr-fallback' in body(text, 'core_alias'), d
            rows.append(dict(model=model, optimization=opt, stackAuto=stack,
                             volatilePhysicalOrder=True, aliasReload=True, otherSpacesIdentical=True))
(a.output/'results.json').write_text(json.dumps(dict(status='PASS', rows=rows,
    compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(), hardwareExecuted=False), indent=2)+'\n')
print('PASS', len(rows), 'member configurations; byte order, aliases, branches, ISR and pointer boundaries')
