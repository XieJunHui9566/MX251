"""Check recovered stack addresses, escaping aliases and physical stores."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
commands, rows = [], []


def body(asm, name):
    return asm.split('symbol=_' + name + ' ', 1)[1].split('; mx251-function end', 1)[0]


boundaries = '''#include "parameter-addresses.h"
void pointer_observe(u8 PTR **);
u32 pa_pointer_store(u32 tag,u8 PTR *p,u8 PTR *q)
{pointer_observe(&p);p=q;pointer_observe(&p);return tag^*p;}
static void swap_signed(s16 *p,s16 *q) {s16 t=*p;*p=*q;*q=t;}
u32 pa_signed(u32 tag,s16 x,s16 y)
{if(tag&1)swap_signed(&x,&y);return tag^(long)x^(long)y;}
u32 pa_join(u32 tag,u16 x,u16 y)
{u16 *p;if(tag&1)p=&x;else p=&y;*p+=7;return tag^((u32)x<<16)^y;}
'''
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('wide-parameter-write.h', 'parameter-addresses.h', 'parameter-addresses-access.c'):
            shutil.copy2(tests / name, d / name)
        (d / 'boundaries.c').write_text(boundaries)
        assemblies = {}
        for source in ('parameter-addresses-access', 'boundaries'):
            command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
                       '--stack-auto', '--mcs251-native-abi', '--mcs251-wide-ordinary',
                       '--mcs251-machine-ir', '--mcs251-function-metadata', '-c', source + '.c']
            q = subprocess.run(command, cwd=d, env=env, capture_output=True)
            (d / (source + '.log')).write_bytes(q.stdout + q.stderr)
            commands.append(dict(command=command, directory=str(d), exitCode=q.returncode))
            assert q.returncode == 0, (command, q.stdout, q.stderr)
            assemblies[source] = (d / (source + '.asm')).read_text()
        asm, limits = assemblies['parameter-addresses-access'], assemblies['boundaries']
        for name in ('pa_ordinary', 'pa_native'):
            assert re.search(r'^; wide parameter-address choice function=' + name + r' .*selected=1 ', asm, re.M), name
        assert re.search(r'^; wide parameter-values choice function=pa_ordinary .*selected=1 ', asm, re.M)
        for name in ('pa_alias', 'pa_partial', 'pa_volatile'):
            section = body(asm, name)
            assert 'wide parameter address entry-offset=' in section, name
            assert not re.search(r'^; wide parameter-value ordinal=2 ', section, re.M), name
        widths = set(map(int, re.findall(r'^; wide parameter-address folded slot=-?\d+ physical=(\d+)', asm, re.M)))
        assert widths == {1, 2, 4}, widths
        pointer = body(limits, 'pa_pointer_store')
        assert 'wide stack parameter store width=3' in pointer and 'wide parameter address entry-offset=' in pointer
        for name in ('pa_pointer_store', 'pa_join'):
            assert not re.search(r'^; wide parameter-value ordinal=[23] ', body(limits, name), re.M), name
        assert 'wide-mir accepted: pa_signed;' in limits
        for text in assemblies.values():
            assert not re.search(r'\br(?:1[6-9]|[23]\d)\b', text), 'Unencodable byte register'
        rows.append(dict(model=model, optimization=opt, foldedWidths=sorted(widths),
                         physicalPointerStore=True, escapingPartialVolatileRetained=True, mergedPointerRetained=True))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows, commands=commands), indent=2) + '\n')
print('PASS: small/large x speed/size; inlined address recovery, pointer stores, escaping/partial/volatile/joined addresses')
