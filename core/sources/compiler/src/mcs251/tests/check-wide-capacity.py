"""Measure generic large-function admission, resource cost and bounded overflow."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'baseline', 'toolchain', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.toolchain.resolve())
env['PATH'] = str(a.toolchain.resolve() / 'bin')+os.pathsep+env['PATH']
rows = []


def source(steps, abi):
    body = '\n'.join(f'x=(x<<3)^(x>>7)^(0x9e370001UL+{k}UL*257UL); y+=x^{k}UL;'
                     for k in range(steps))
    return f'unsigned long probe(unsigned long seed) __sdcccall({abi}) {{\n' \
           'unsigned long x=seed,y=0;\n'+body+'\nreturn x^y;\n}\n'


def peak_memory(process):
    if os.name != 'nt':
        return None
    class Counters(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
            (n, ctypes.c_size_t) for n in ('peakWorkingSet', 'workingSet', 'peakPaged',
                'paged', 'peakNonpaged', 'nonpaged', 'pagefile', 'peakPagefile')]
    info = Counters()
    info.cb = ctypes.sizeof(info)
    get = ctypes.WinDLL('psapi').GetProcessMemoryInfo
    get.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    if not get(wintypes.HANDLE(int(process._handle)), ctypes.byref(info), info.cb):
        raise ctypes.WinError()
    return info.peakWorkingSet


def compile_one(name, compiler, model, opt, abi, steps, values, nodes=2048):
    out = a.output / name
    out.mkdir()
    (out / 'probe.c').write_text(source(steps, abi))
    env.update(MX251_WIDE_VALUE_LIMIT=str(values), MX251_WIDE_NODE_LIMIT=str(nodes))
    start = time.perf_counter()
    command = [str(compiler.resolve()), '-mmcs251', '--model-'+model, '--opt-code-'+opt,
               '--std-sdcc11', '--stack-auto', '--mcs251-wide-ordinary',
               '--mcs251-machine-ir', '--mcs251-function-metadata', '-c', 'probe.c']
    process = subprocess.Popen(command, cwd=out, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate()
    elapsed = time.perf_counter()-start
    peak = peak_memory(process)
    (out / 'compile.log').write_bytes(stdout+stderr)
    assert process.returncode == 0, (name, stderr)
    asm = (out / 'probe.asm').read_text()
    rel = (out / 'probe.rel').read_bytes()
    row = dict(name=name, model=model, optimization=opt, abi=abi, steps=steps, valueLimit=values,
               nodeLimit=nodes, seconds=elapsed, peakWorkingSetBytes=peak,
               codeBytes=int(re.search(rb'^A CSEG size ([0-9a-fA-F]+)', rel, re.M)[1], 16),
               admitted='wide-mir accepted: probe' in asm,
               refusals=re.findall(r'; wide refusal function=probe stage=\S+ reason=(\S+)', asm))
    imported = re.search(r'; wide imported function=probe values=(\d+) nodes=(\d+)', asm)
    if imported:
        row.update(values=int(imported[1]), nodes=int(imported[2]))
    rows.append(row)
    print(json.dumps(row), flush=True)
    return row, rel


for model, opt, abi in [('small', 'size', 2), ('small', 'speed', 2), ('large', 'size', 2),
                         ('small', 'size', 4), ('small', 'speed', 4), ('large', 'size', 4)]:
    prefix = f'{model}-{opt}-abi{abi}'
    before, old = compile_one(prefix+'-before', a.baseline, model, opt, abi, 96, 512)
    bounded, same = compile_one(prefix+'-512', a.sdcc, model, opt, abi, 96, 512)
    after, new = compile_one(prefix+'-1024', a.sdcc, model, opt, abi, 96, 1024)
    assert before['refusals'] == bounded['refusals'] == ['value-capacity']
    assert old == same, prefix+' changed existing fallback code'
    assert after['admitted'] and after['values'] > 512 and after['codeBytes'] < before['codeBytes']

for name, steps, values, nodes, reason in [('value-overflow', 176, 1024, 2048, 'value-capacity'),
                                          ('node-overflow', 96, 1024, 512, 'node-capacity')]:
    row, _ = compile_one(name, a.sdcc, 'small', 'speed', 2, steps, values, nodes)
    assert row['refusals'] == [reason] and not row['admitted']

(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows,
    note='Single cold process per configuration; elapsed time and peak working set are observations.'), indent=2))
print('PASS: six large-function configurations, exact legacy fallback, value and node overflow')
