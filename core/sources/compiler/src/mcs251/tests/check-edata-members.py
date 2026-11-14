"""Compile fixed EDATA members; check physical access widths and host results."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def body(text, name):
    return re.search(r'^_(?:__m251r4_)?'+name+r':\n(.*?)^; mx251-function end', text, re.M | re.S)[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdcc', 'baseline', 'support', 'gcc', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    tests = Path(__file__).resolve().parent
    files = ['edata-members.h', 'edata-members.c', 'edata-members-mutate.c',
             'edata-members-init.c', 'edata-members-format.c']
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
    env['SDCC_HOME'] = str(a.support.resolve())
    env['PATH'] = str(a.support.resolve()/'bin')+';'+env['PATH']
    negative = '''#include "edata-members.h"
__xdata MemberBlock far_block;
const __code u8 code_values[2]={1,2};
u16 dynamic_member(MemberBlock *p) NATIVE { return p->word; }
u8 dynamic_index(u8 i) NATIVE { return member_block.bytes[i]; }
u32 far_member(void) NATIVE { return far_block.dword; }
u8 code_member(void) NATIVE { return code_values[1]; }
'''
    rows = []
    for model in ('small', 'large'):
        for opt in ('speed', 'size'):
            sizes = {}
            for label, cc in [('parent', a.baseline), ('candidate', a.sdcc)]:
                d = a.output/(model+'-'+opt+'-'+label)
                d.mkdir()
                for name in files:
                    shutil.copy2(tests/name, d/name)
                shutil.copy2(tests/'wide-stack-direct.c', d/'wide-stack-direct.c')
                (d/'other-spaces.c').write_text(negative)
                for name in [n for n in files if n.endswith('.c')] + ['other-spaces.c', 'wide-stack-direct.c']:
                    command = [str(cc.resolve()), '-mmcs251', '--model-'+model,
                        '--opt-code-'+opt, '--stack-auto', '--mcs251-wide-ordinary',
                        '--mcs251-native-abi', '--mcs251-machine-ir', '--mcs251-function-metadata', '-c', name]
                    run = subprocess.run(command, cwd=d, env=env, capture_output=True)
                    (d/(name+'.log')).write_bytes(run.stdout+run.stderr)
                    assert run.returncode == 0, (d, name, run.stderr)
                text = (d/'edata-members.asm').read_text()
                sizes[label] = int(re.search(r'^A CSEG size ([0-9A-Fa-f]+)',
                    (d/'edata-members.rel').read_text(), re.M)[1], 16)
                if label != 'candidate':
                    continue
                for name in ['member_write', 'member_read', 'member_pointer', 'member_branch',
                             'member_volatile', 'member_alias', 'member_work', 'member_native']:
                    assert 'e:(' in body(text, name), name
                # The pointer occupies offsets 7..9; offset 10 is its neighbour.
                pointer = body(text, 'member_pointer')
                assert re.findall(r'e:\(_member_block\+(\d+)\)', pointer) == ['7', '8'], pointer
                volatile = body(text, 'member_volatile')
                trace = []
                for line in volatile.splitlines():
                    m = re.search(r'\bmov\s+(.*)', line)
                    if not m:
                        continue
                    operands = m[1].split(',')
                    for side, operand in enumerate(operands):
                        field = re.fullmatch(r'e:\(_member_device\+(\d+)\)', operand)
                        if field:
                            trace.append(('store' if side == 0 else 'load', int(field[1])))
                expected = [('store', n) for n in range(11)]
                expected += [('load', n) for n in [3,4,5,6,1,2,0,10,7,8,9]]
                assert trace == expected, trace
                other = (d/'other-spaces.asm').read_text()
                for name in ['dynamic_member', 'dynamic_index', 'far_member', 'code_member']:
                    assert 'e:(' not in body(other, name), name
            assert sizes['candidate'] < sizes['parent'], sizes
            stack_name = 'wide-stack-direct.rel'
            assert (a.output/(model+'-'+opt+'-parent')/stack_name).read_bytes() == (d/stack_name).read_bytes()
            rows.append(dict(model=model, optimization=opt, bytes=sizes, volatileByteTransactions=22))
    host = a.output/'host'
    host.mkdir()
    for name in files:
        shutil.copy2(tests/name, host/name)
    (host/'main.c').write_text('''#include <stdio.h>
#include "edata-members.h"
int main(void) {
    unsigned kind, seed;
    for(kind=0;kind<2;kind++) for(seed=0;seed<64;seed++)
        printf("%u %u %lu\\n",kind,seed,(unsigned long)(kind?member_native(seed):member_work(seed)));
    return 0;
}
''')
    subprocess.run([str(a.gcc.resolve()), '-std=c99', '-O2', 'main.c',
        *[n for n in files if n.endswith('.c')], '-o', 'reference.exe'], cwd=host, check=True)
    output = subprocess.check_output([str((host/'reference.exe').resolve())], text=True)
    reference = {f'{k}:{s}':int(v) for k,s,v in (line.split() for line in output.splitlines())}
    (a.output/'reference.json').write_text(json.dumps(reference, indent=2)+'\n')
    result = dict(status='PASS', compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),
        rows=rows, hostCases=len(reference), hardwareExecuted=False)
    (a.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
