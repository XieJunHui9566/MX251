"""Compile stack-parameter promotion under both ABIs and all model/size modes."""
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


boundaries = '''#include "parameter-values.h"
u32 pv_volatile(u32 tag,volatile u16 x,u8 n)
{while(n){tag^=x;x+=n;--n;}return tag^x;}
u32 pv_volatile_pointer(u32 tag,u8 * volatile p,u8 n)
{while(n){tag^=*p;++p;--n;}return tag^*p;}
u32 pv_unused_first(u32 unused,u16 x,u8 n)
{while(n){if(x&1)x+=n;else x^=0x8001;--n;}return (u32)x+x;}
'''
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        d = a.output / (model + '-' + opt)
        d.mkdir()
        for name in ('wide-parameter-write.h', 'parameter-values.h',
                     'wide-parameter-write-callees.c', 'parameter-values-access.c'):
            shutil.copy2(tests / name, d / name)
        (d / 'boundaries.c').write_text(boundaries)
        assemblies = {}
        for source in ('wide-parameter-write-callees', 'parameter-values-access', 'boundaries'):
            command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--opt-code-' + opt,
                       '--stack-auto', '--mcs251-native-abi', '--mcs251-wide-ordinary',
                       '--mcs251-machine-ir', '--mcs251-function-metadata', '-c', source + '.c']
            q = subprocess.run(command, cwd=d, env=env, capture_output=True)
            (d / (source + '.log')).write_bytes(q.stdout + q.stderr)
            commands.append(dict(command=command, directory=str(d), exitCode=q.returncode))
            assert q.returncode == 0, (command, q.stdout, q.stderr)
            assemblies[source] = (d / (source + '.asm')).read_text()
        native, ordinary, limits = (assemblies[s] for s in
            ('wide-parameter-write-callees', 'parameter-values-access', 'boundaries'))
        for name, asm in [('p_loop', native), ('p_float', native), ('p_pointer', native),
                          ('pv_ordinary', ordinary), ('pv_switch', ordinary)]:
            assert re.search(r'^; wide parameter-values choice function=' + name + r' .*selected=1 ', asm, re.M), name
        assert 'wide jump table entries=' in body(ordinary, 'pv_switch')
        alias = body(ordinary, 'pv_alias')
        assert 'wide parameter address entry-offset=-4' in alias
        assert not re.search(r'^; wide parameter-value ordinal=2 ', alias, re.M)
        for name in ('pv_volatile', 'pv_volatile_pointer'):
            assert not re.search(r'^; wide parameter-value ordinal=2 ', body(limits, name), re.M)
        assert 'wide-mir accepted: pv_unused_first;' in limits
        widths = set(map(int, re.findall(r'^; wide parameter-value ordinal=\d+ physical=(\d+)', native, re.M)))
        assert widths == {1, 2, 3, 4}, widths
        assert 'wide ordinary ABI2 entry: first=DPL/DPH/B/A stack=physical' in ordinary
        pointer_loads = 0
        for asm in (native, ordinary):
            for match in re.finditer(r'; wide stack parameter direct reg=(\d+) physical=3 logical=4 entry-offset=-?\d+\n', asm):
                reg = int(match[1])
                load = re.match(r'\s*mov\s+a,(@spx[^\n]*)\n\s*movz\s+wr(\d+),a\n\s*mov\s+wr(\d+),(@spx[^\n]*)\n', asm[match.end():])
                assert load and int(load[2]) == reg and int(load[3]) == reg + 2
                assert not re.match(r'\s*anl\s+wr\d+,#0x00ff', asm[match.end()+load.end():])
                pointer_loads += 1
        assert pointer_loads >= 4, pointer_loads
        # Arbitrary parameter arithmetic still normalizes the 24-bit result.
        assert re.search(r'\banl\s+wr\d+,#0x00ff', body(ordinary, 'pv_ordinary'))
        for asm in assemblies.values():
            assert not re.search(r'\br(?:1[6-9]|[23]\d)\b', asm), 'Unencodable byte register'
        rows.append(dict(model=model, optimization=opt, promotedWidths=sorted(widths),
                         pointerDirectLoads=pointer_loads, jumpTable=True,
                         addressedParameterRetained=True, volatileParametersRetained=True))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows, commands=commands), indent=2) + '\n')
print('PASS: small/large x speed/size; ABI2/4; 1/2/3/4-byte slots; loops, switch, addressed and volatile parameters')
