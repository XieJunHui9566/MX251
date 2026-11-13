"""Compile predicates, check encoded branch distances and volatile read widths."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'cc', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('machine-board.h', 'predicate-branches.c'):
            shutil.copy2(tests / name, d / name)
        (d / 'preserve.txt').write_text('MX251-CALLEE-PRESERVE-1\n'
            'global ___m251r4_pb_loads 4 ff00f300\n')
        command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model,
                   '--opt-code-' + opt, '--stack-auto', '--mcs251-wide-ordinary',
                   '--mcs251-function-metadata', '--mcs251-machine-ir',
                   '--mcs251-callee-preserve', 'preserve.txt', '-c', 'predicate-branches.c']
        q = subprocess.run(command, cwd=d, env=env, capture_output=True)
        (d / 'compile.log').write_bytes(q.stdout + q.stderr)
        assert q.returncode == 0, q.stderr
        asm = (d / 'predicate-branches.asm').read_text()
        assert asm.count('; wide predicate branch ') >= 18
        assert '; mx251-callee-preserved schema=1 saved=ff00f300' in asm
        loads = asm.split('___m251r4_pb_loads:', 1)[1].split('; mx251-function end', 1)[0]
        reads = lambda symbol: re.findall(r'^\s*mov\s+[^,\n]+,\(_' + symbol + r'\+(\d+)\)', loads, re.M)
        assert reads('pb_byte') == ['0', '0']
        assert reads('pb_word') == ['0', '1']
        assert reads('pb_long') == ['0', '1', '2', '3'] * 2
        # Check actual encoded displacements, including branches left long by
        # the emitter and subsequently simplified by the machine pipeline.
        labels, branches = {}, []
        for line in (d / 'predicate-branches.lst').read_text().splitlines():
            if len(line) < 39 or not re.fullmatch('[0-9A-F]{6}', line[6:12]):
                continue
            address = int(line[6:12], 16)
            text = line[39:].strip()
            data = bytes.fromhex(' '.join(re.findall('[0-9A-F]{2}', line[12:29])))
            if text.endswith(':'):
                labels[text[:-1]] = address
            m = re.match(r'(jb|jnb)\s+([^,]+),(\S+)', text)
            if m:
                branches.append((address, data, m[3], m[2]))
        assert len(branches) >= 12
        directions = set()
        for address, data, target, operand in branches:
            delta = labels[target] - address - len(data)
            assert len(data) == 3 and -128 <= delta <= 127, (data, delta)
            assert (data[-1] if data[-1] < 128 else data[-1] - 256) == delta
            directions.add('backward' if delta < 0 else 'forward')
        assert directions == {'forward', 'backward'}
        assert not re.search(r'\br(?:1[6-9]|[23]\d)\b', asm)
        rows.append(dict(model=model, optimization=opt, command=command,
                         bitBranches=len(branches), volatileReadWidths=True))

driver = r'''
#include <assert.h>
#include <stdio.h>
#include "instruction_effects.h"
unsigned mcs251SphAddress=0xbe;
int main(void) {
  const char *body="__wm_target:\n";
  for(int bit=0;bit<8;++bit)for(int invert=0;invert<2;++invert) {
    char s[64];snprintf(s,sizeof s,"%s acc.%d,__wm_target",invert?"jnb":"jb",bit);
    mx251_effects e=mx251InstructionEffects(s,strlen(s),body,strlen(body));
    assert(!e.unknown && e.reads==(UINT32_C(1)<<MCS251_BYTE_A) && !e.writes);
    assert(!e.flagsReads && !e.flagsWrites && !e.memory && !e.specialWrites);
  }
  const char *bad[]={"jb acc.8,__wm_target","jb acc.0,missing","jb acc.1x,__wm_target","jbc acc.2,__wm_target"};
  for(unsigned i=0;i<sizeof bad/sizeof *bad;++i)
    assert(mx251InstructionEffects(bad[i],strlen(bad[i]),body,strlen(body)).unknown);
  return 0;
}
'''
(a.output / 'effects.c').write_text(driver)
command = [str(a.cc.resolve()), '-std=c17', '-O2', '-I' + str(tests.parent),
           str(a.output / 'effects.c'), '-o', str(a.output / 'effects.exe')]
q = subprocess.run(command, capture_output=True)
(a.output / 'effects.log').write_bytes(q.stdout + q.stderr)
assert q.returncode == 0, q.stderr
subprocess.run([str((a.output / 'effects.exe').resolve())], check=True)
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows,
    effectsCommand=command, effectsCases=20), indent=2) + '\n')
print('PASS: 4 configurations; encoded bit branches, full volatile reads, preserved stack frame and instruction effects')
