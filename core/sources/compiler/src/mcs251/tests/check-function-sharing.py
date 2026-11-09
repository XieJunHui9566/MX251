"""Differential execution of shared machine templates and rejection boundaries.

The small instruction interpreter checks observable registers, caller stack,
balanced returns and branches after temporary restores. Actual 251 encoding and
CODE-pointer accesses are covered separately by the board renderer fixture.
"""
import argparse
from pathlib import Path
import random
import re
import subprocess
import json


def function(n):
    name = f'_sample{n}'
    pad = '\n'.join('\tnop' for _ in range(110))
    return f'''\
; mx251-function begin schema=1 symbol={name} isr=0 addressTaken=1 opaque=0 indirect=0
; mx251-scalar-definition schema=1 symbol={name} signature=u32(u32,u16)
{name}:
 add spx,#12
 mov wr20,#0x5a5a
 mov wr22,#0xa5a5
 mov wr0,wr10
 mov a,#((_data{n}+0)>>16)
 mov wr4,#(_data{n}+0)
 mov wr2,@spx-16
 mov @spx-1,wr2
 mov r14,#{17+n}
 mov r6,#0
{pad}
__wm_sample{n}_loop:
 cmp r6,#{4+n}
 jnc __wm_sample{n}_done
 add wr2,#{0x1234+n}
 sub wr4,#{0x4321+n}
 xrl wr2,#{0x8420+n}
 anl wr4,#{0xff00+n}
 orl r14,#{0x40+n}
 cmp wr2,#{0x5678+n}
 je __wm_sample{n}_mark
 add dr8,#{255+n}
__wm_sample{n}_mark:
 push wr2
 mov wr12,@spx-3
 pop wr2
 inc r6,#1
 sjmp __wm_sample{n}_loop
__wm_sample{n}_done:
 mov wr10,wr0
 mov wr12,@spx-16
 sub spx,#12
 eret
; mx251-bank-contract schema=1 symbol={name} abi=2 bank=0 naked=0
; mx251-backend-choice function={name} selected=wide wide-bytes=300
; mx251-function end schema=1 symbol={name}
'''


def execute(text, name, seed):
    instructions, labels = [], {}
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if not line:
            continue
        if line.endswith(':'):
            labels[line[:-1]] = len(instructions)
        else:
            op, *args = line.split(None, 1)
            instructions.append((op, args[0].split(',') if args else []))
    randomizer = random.Random(seed)
    regs = bytearray(randomizer.randrange(256) for _ in range(32))
    memory = bytearray(8192)
    for i in range(0x1000 - 16, 0x1001):
        memory[i] = randomizer.randrange(256)
    sp = 0x1000
    carry = zero = False
    pc = labels[name]

    def register(s):
        if s == 'a':
            return 11, 1
        if s == 'b':
            return 10, 1
        match = re.fullmatch(r'(w|d)?r(\d+)', s)
        assert match, s
        return int(match[2]), {'w': 2, 'd': 4, None: 1}[match[1]]

    def immediate(s):
        s = re.sub(r'_data([0-2])', lambda m: str(0x231234 + int(m[1]) * 0x12345), s)
        assert re.fullmatch(r'[0-9a-fx()+> ]+', s), s
        return eval(s, {'__builtins__': {}}, {})

    def address(s):
        assert s.startswith('@spx'), s
        return sp + (int(s[4:], 0) if s[4:] else 0)

    def read(s, width=None):
        if s == 'spx':
            return sp
        if s.startswith('#'):
            return immediate(s[1:])
        if s.startswith('@'):
            assert width
            at = address(s)
            return int.from_bytes(memory[at:at + width], 'big')
        first, width = register(s)
        return int.from_bytes(regs[first:first + width], 'big')

    def write(s, value, width=None):
        nonlocal sp
        if s == 'spx':
            sp = value
            return
        if s.startswith('@'):
            assert width
            at = address(s)
            memory[at:at + width] = (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'big')
            return
        first, width = register(s)
        regs[first:first + width] = (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'big')

    for _ in range(5000):
        op, args = instructions[pc]
        pc += 1
        if op == 'eret':
            return bytes(regs), bytes(memory[0x1000 - 16:0x1001]), sp - 3
        if op == 'nop':
            continue
        if op in ('ejmp', 'sjmp', 'jc', 'jnc', 'je', 'jne'):
            take = {'ejmp': True, 'sjmp': True, 'jc': carry, 'jnc': not carry,
                    'je': zero, 'jne': not zero}[op]
            if take:
                pc = labels[args[0]]
            continue
        a = args[0]
        if op in ('push', 'pushw'):
            width = 2 if op == 'pushw' else register(a)[1]
            memory[sp + 1:sp + 1 + width] = (read(a) & ((1 << (width * 8)) - 1)).to_bytes(width, 'big')
            sp += width
            continue
        if op == 'pop':
            width = register(a)[1]
            write(a, int.from_bytes(memory[sp - width + 1:sp + 1], 'big'))
            sp -= width
            continue
        b = args[1]
        width = register(b)[1] if a.startswith('@') else 4 if a == 'spx' else register(a)[1]
        value = read(b, width)
        if op in ('mov', 'movz'):
            write(a, value, width)
            continue
        old = read(a, width)
        if op == 'cmp':
            carry, zero = old < value, old == value
            continue
        result = {'add': lambda: old + value, 'sub': lambda: old - value,
                  'anl': lambda: old & value, 'orl': lambda: old | value,
                  'xrl': lambda: old ^ value, 'inc': lambda: old + value}[op]()
        carry = result < 0 or result >= 1 << (width * 8)
        zero = (result & ((1 << (width * 8)) - 1)) == 0
        write(a, result, width)
    raise AssertionError('non-terminating transformed function')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gcc', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    backend = Path(__file__).resolve().parents[1]
    util = backend.parents[1] / 'support/util'
    driver = a.output / 'driver.c'
    driver.write_text('''\
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "dbuf.h"
#include "dbuf_string.h"
#define Safe_alloc(n) calloc(1,n)
#define Safe_realloc(p,n) realloc(p,n)
#define Safe_free(p) free(p)
#define SNPRINTF snprintf
#include "function_share.h"
static int barrier(const char *s) { return !strcmp(s,"_StackAlias"); }
int main(void) {
  struct dbuf_s buf; dbuf_init(&buf,4096); int c;
  while((c=getchar())!=EOF){char b=(char)c;dbuf_append(&buf,&b,1);}
  mcs251ShareFunctions(&buf,barrier);
  fwrite(dbuf_get_buf(&buf),1,dbuf_get_length(&buf),stdout);
  dbuf_destroy(&buf);return 0;
}
''')
    exe = a.output / 'driver.exe'
    subprocess.run([str(a.gcc), '-std=c11', '-O2', '-I' + str(backend), '-I' + str(util),
                    str(driver), str(util / 'dbuf.c'), str(util / 'dbuf_string.c'), '-o', str(exe)], check=True)

    def transform(text):
        return subprocess.run([str(exe)], input=text.encode(), capture_output=True, check=True).stdout.decode().replace('\r\n', '\n')

    original = ''.join(function(i) for i in range(3))
    shared = transform(original)
    (a.output / 'original.asm').write_text(original)
    (a.output / 'shared.asm').write_text(shared)
    assert shared.count('; mx251-function-sharing ') == 3
    for seed in range(256):
        for i in range(3):
            assert execute(original, f'_sample{i}', seed) == execute(shared, f'_sample{i}', seed), (seed, i)
    # Different calls/control flow/layout/signature cannot share a template.
    base = function(0)
    cases = {
        'one-function': base,
        'signature': base + function(1).replace('u32(u32,u16)', 'u16(u32,u16)'),
        'frame': base + function(1).replace('spx,#12', 'spx,#14'),
        'control-flow': base + function(1).replace('jnc __wm_sample1_done', 'jc __wm_sample1_done'),
        'abi4': base + function(1).replace('abi=2', 'abi=4'),
        'isr': base + function(1).replace('isr=0', 'isr=1'),
        'opaque': base + function(1).replace('opaque=0', 'opaque=1'),
        'stack-alias': (base + function(1)).replace('mov wr0,wr10', 'mov a,_StackAlias'),
        'frame-escape': (base + function(1)).replace('mov wr0,wr10', 'mov dr0,spx'),
        'external-jump': (base + function(1)).replace('sjmp __wm_sample', 'sjmp _unknown'),
    }
    for name, text in cases.items():
        assert transform(text) == text, name
    result = dict(status='PASS', differentialExecutions=768, rejectionCases=list(cases))
    (a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
