"""Same-source user workloads on MX251/Keil, timed by the board's Timer0.

The measured region includes dispatch, one workload and result transport.
UART output and input preparation are outside it. Report raw ticks; the empty
control documents harness cost but is not subtracted from either compiler.
"""
import argparse
import json
from pathlib import Path
import shutil
import statistics

import machine_pipeline_fixture as board
import unified_sdk_matrix as matrix
import user_stress

KINDS = [name + '_' + variant for name in user_stress.CASES
         for variant in ('clean', 'messy')] + ['empty_control']


def create(root, gcc):
    root.mkdir(parents=True, exist_ok=False)
    source = root / 'source'
    user_stress.generate(source)
    header = (source / 'stress.h').read_text().replace(
        '#ifdef __SDCC', '#if defined(__SDCC) || defined(__C251__)')
    header = header.replace('/* No vendor', '#ifdef __C251__\n#define __edata edata\n#endif\n\n/* No vendor')
    (source / 'stress.h').write_text(header)
    # C251 requires the memory qualifier after a typedef name; MX251 also
    # accepts that spelling. Both compilers receive these identical sources.
    for name, typename in [('bits.c', 'Flags'), ('fields.c', 'Record'), ('ring.c', 'Queue')]:
        path = source / name
        path.write_text(path.read_text().replace('__edata '+typename, typename+' __edata'))
    host = root / 'host'
    shutil.copytree(source, host)
    (host / 'host.c').write_text('#include <stdio.h>\n#include "stress.h"\n'
        'int main(void) { unsigned k,s; for(k=0;k<12;k++) for(s=0;s<64;s++) '
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s)); return 0; }\n')
    user_stress.run([gcc, '-std=c99', '-O2', '-Wall', *sorted(p.name for p in host.glob('*.c')),
                     '-o', 'reference.exe'], host, label='compile')
    values = {k: int(v) for k, v in (line.split() for line in
              user_stress.run([host / 'reference.exe'], host).splitlines())}
    for kind in range(0, 12, 2):
        for seed in range(64):
            assert values[f'{kind}:{seed}'] == values[f'{kind+1}:{seed}']
    values.update({f'12:{seed}': seed for seed in range(64)})
    matrix.save(root / 'reference.json', values)

    project = root / 'mx251'
    shutil.copytree(source, project)
    main = (user_stress.SOURCES.parent / 'machine-board.c').read_text().replace(
        '"machine-board.h"', '"stress.h"')
    start, end = main.index('        x0 = '), main.index('        TH0 = ')
    main = main[:start] + main[end:]
    start, end = main.index('        if (kind == 0)'), main.index('        TR0 = 0; ticks')
    main = main[:start] + '        result = kind == 12 ? input : stress_run(kind,input);\n' + main[end:]
    main = main.replace('kind < 2', 'kind < 13').replace(
        'u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (project / 'board.c').write_text(main)
    files = sorted(p.name for p in project.iterdir() if p.suffix in ('.c', '.h'))
    units = [dict(name='u'+str(i), file=n, backend='ordinary-wide', stackAuto=True, defines=[])
             for i, n in enumerate(files) if n.endswith('.c')]
    matrix.save(project / 'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11',
        runtime='standard', defines=[], files=files, units=units, archives=[],
        link=[u['name'] for u in units]+['@startup']))
    (project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\n'
        'project(user_timing NONE)\ninclude("${MX251_PROJECT_MODULE}")\n'
        'add_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')
    matrix.save(root / 'source.json', dict(kinds=KINDS, seeds=64,
        generator=matrix.pin(Path(__file__)), upstreamGenerator=matrix.pin(Path(user_stress.__file__)),
        sourceFiles=[matrix.pin(project / n) for n in files],
        changes='Only portable type/address-space declarations and board harness; workload bodies unchanged.',
        timer='Timer0 mode 1, 12T, interrupts disabled; same board and flash clock for both images.',
        timingScope='Dispatch, one workload and result transport. No UART. Raw ticks; no baseline subtraction.'))


def keil(root, kit):
    base = root / 'reference-build'
    native = base / 'native'
    native.mkdir(parents=True, exist_ok=False)
    files = sorted(p for p in (root / 'mx251').iterdir() if p.suffix in ('.c', '.h'))
    for p in files:
        shutil.copy2(p, native / p.name)
    (base / 'sample.uvproj').write_text('<Project><Files/></Project>')
    matrix.save(base / 'original-inputs.json', dict(sources=[p.name for p in files if p.suffix == '.c'],
        assembly=[], options='<Target251><Target251Misc><uFrame4>0</uFrame4></Target251Misc></Target251>',
        defines='', optLevel=7, project=matrix.pin(base / 'sample.uvproj')))
    config = dict(keil=str(kit), keilVectorAdapterIds=[],
                  keilClasses='CLASSES(EDATA(0-0x7FF),HDATA(0-0x7FF))')
    out = base / 'keil'
    out.mkdir()
    result = matrix.build_keil(config, dict(id='user-timing', historicalKeilBytes=None), out)
    result['tools'] = [matrix.pin(kit / 'BIN' / n) for n in ('C251.EXE', 'L251.EXE', 'OH251.EXE')]
    matrix.save(out / 'result.json', result)
    print(json.dumps(result))


def check(data, reference):
    samples, values, mismatches, overflows = {}, {}, [], []
    for pos in range(len(data)-15):
        frame = data[pos:pos+16]
        if frame[:4] != b'MIR1' or frame[-2:] != b'\r\n':
            continue
        checksum = 0
        for byte in frame[:14]:
            checksum ^= byte
        if checksum:
            continue
        key = f'{frame[4]}:{frame[5]}'
        value = int.from_bytes(frame[6:10], 'big')
        if key not in reference or value != reference[key]:
            mismatches.append(dict(key=key, actual=value, expected=reference.get(key)))
        if frame[12]:
            overflows.append(key)
        samples.setdefault(key, []).append(int.from_bytes(frame[10:12], 'big'))
        values[key] = value
    missing = sorted(set(reference)-samples.keys())
    minimum = min(map(len, samples.values()), default=0)
    valid = not missing and not mismatches and not overflows and minimum >= 3
    return dict(status='PASS' if valid else 'FAIL', cases=len(samples), expectedCases=len(reference),
        frames=sum(map(len, samples.values())), minimumSamples=minimum, missing=missing,
        mismatches=mismatches, overflows=overflows, timingQualified=valid,
        ticks={k: statistics.median(v) for k, v in samples.items()},
        ranges={k: [min(v), max(v), len(v)] for k, v in samples.items()}, values=values)


def capture(root, suite, image):
    reference = matrix.read(root / 'reference.json')
    output = root / (suite+'-capture')
    board.capture(image, output, lambda data: check(data, reference), max_frames=len(reference)*4+256)
    assert matrix.read(output / 'result.json')['status'] == 'PASS', output


def compare(root):
    mx, keil = [matrix.read(root / (s+'-capture') / 'result.json') for s in ('mx251', 'keil')]
    assert mx['status'] == keil['status'] == 'PASS'
    assert mx['device']['serial'] == keil['device']['serial']
    clocks = {}
    for suite in ('mx251', 'keil'):
        flash = root / (suite+'-capture') / 'flash'
        plan = matrix.read(flash / 'plan.json')
        trace = [json.loads(line) for line in (flash / 'trace.ndjson').read_text().splitlines()]
        measured = [row['measuredHz'] for row in trace if row.get('stage') == 'calibrated']
        assert len(measured) == 1 and measured[0] > 0
        clocks[suite] = dict(model=plan['model'], requestedHz=plan['ircHz'], measuredHz=measured[0],
                             plan=matrix.pin(flash / 'plan.json'), trace=matrix.pin(flash / 'trace.ndjson'))
    assert clocks['mx251']['model'] == clocks['keil']['model']
    assert clocks['mx251']['requestedHz'] == clocks['keil']['requestedHz']
    clock_ratio = clocks['keil']['measuredHz'] / clocks['mx251']['measuredHz']
    for p in (root / 'mx251').glob('*'):
        if p.suffix in ('.c', '.h'):
            assert p.read_bytes() == (root / 'reference-build/keil' / p.name).read_bytes(), p
    rows = []
    for kind, name in enumerate(KINDS):
        a, b = [[r['ticks'][f'{kind}:{seed}'] for seed in range(64)] for r in (mx, keil)]
        ratios = [x/y*clock_ratio if y else None for x, y in zip(a, b)]
        row = dict(kind=kind, name=name, mxTicks=sum(a), keilTicks=sum(b),
            aggregateRatio=sum(a)/sum(b) if sum(b) else None,
            clockAdjustedRatio=sum(a)/sum(b)*clock_ratio if kind < 12 else None,
            maxCaseRatio=max((x for x in ratios if x is not None), default=None),
            casesAt110=sum(x is not None and x <= 1.10 for x in ratios))
        rows.append(row)
        print(json.dumps(row))
    matrix.save(root / 'timing-comparison.json', dict(status='MEASURED_USER_KERNELS', rows=rows,
        fullEngineeringTimingQualified=False, clocks=clocks, source=matrix.pin(root / 'source.json'),
        imageBytes={s: matrix.image(root / ('mx251/.build/mx251/latest/firmware.hex' if s == 'mx251'
                      else 'reference-build/keil/firmware.hex'))[0]['payloadBytes'] for s in ('mx251', 'keil')},
        captures=[matrix.pin(root / (s+'-capture') / 'result.json') for s in ('mx251', 'keil')]))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('--gcc', type=Path, required=True)
    p = sub.add_parser('keil'); p.add_argument('--kit', type=Path, required=True)
    p = sub.add_parser('capture'); p.add_argument('--suite', choices=['mx251', 'keil'], required=True)
    p.add_argument('--hex', type=Path, required=True)
    sub.add_parser('compare')
    for p in sub.choices.values():
        p.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create': create(args.root, args.gcc)
    elif args.command == 'keil': keil(args.root, args.kit)
    elif args.command == 'capture': capture(args.root, args.suite, args.hex)
    else: compare(args.root)
