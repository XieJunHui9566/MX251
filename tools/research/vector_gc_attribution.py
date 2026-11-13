"""Attribute final images before/after vector-aware function collection."""
import argparse
from collections import Counter
from pathlib import Path
import re
from call_layout import ihex
from focus_size_band import functions, pin, read
from unified_sdk_matrix import save


def image_parts(root):
    memory = ihex(root/'firmware.hex')
    funcs, inputs = functions(root, memory)
    mapping = (root/'linked.partial.map').read_text('latin1')
    areas = {}
    for name, address, size in re.findall(r'^(\S+)\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+=', mapping, re.M):
        address, size = int(address, 16), int(size, 16)
        areas[name] = sum(address <= a < address+size for a in memory)
    app = sum(f['bytes'] for f in funcs)
    const = sum(areas.get(n, 0) for n in ('CONST', 'XINIT'))
    cseg = areas.get('CSEG', 0)
    gc = read(root/'function-gc/manifest.json')
    inputs += [pin(root/name) for name in ('firmware.hex', 'linked.partial.map',
        'project.snapshot.json', 'function-gc/manifest.json')]
    return dict(bytes=len(memory), functions=funcs,
        partition=dict(applicationInstructions=app, otherCseg=cseg-app,
                       constants=const, startupVectorsAndOther=len(memory)-cseg-const),
        libraryMembers=re.findall(r'\.lib\s*\[\s*(\S+)\s*\]', mapping),
        gc={key: gc.get(key) for key in ('status','issues','removedFunctions','resolvedAbsoluteTargets')},
        inputs=inputs)


def keil_parts(root):
    memory = ihex(root/'firmware.hex')
    segments, totals, funcs = [], Counter(), {}
    for line in (root/'FIRMWARE.MAP').read_text('latin1').splitlines():
        m = re.match(r'^([0-9A-F]+)H\s+([0-9A-F]+)H\s+([0-9A-F]+)H\s+\S+\s+\S+\s+(CODE|HCONST|CONST)\s+(\S+)', line)
        if not m:
            continue
        name, size = m[5], int(m[3], 16)
        function = re.match(r'\?PR\?([^?]+)\?', name)
        category = ('startupVectors' if name.upper().startswith(('?CO?START251', '?C_C51STARTUP', '?CO?VECTORS?', '?CO?ISR?'))
                    else 'functionSegments' if function else 'libraries' if name == '?C?LIB_CODE' else 'constants')
        if function:
            funcs[function[1]] = size
        totals[category] += size
        segments.append(dict(name=name, bytes=size, category=category, memoryClass=m[4]))
    assert sum(totals.values()) == len(memory)
    return dict(bytes=len(memory), partition=dict(totals), functions=funcs, segments=segments,
                inputs=[pin(root/name) for name in ('firmware.hex','FIRMWARE.MAP')])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    p.add_argument('--references', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a=p.parse_args()
    rows=[]
    for key in ('p077','p079'):
        suffix=Path('projects')/key/'mx251-native/.build/mx251/latest'
        variants=[read(a.after/'projects'/key/suite/'result.json')
                  for suite in ('mx251-native','mx251-standard')]
        assert all(v['status']=='BUILT' for v in variants)
        suite=min(variants,key=lambda v:v['payloadBytes'])['suite']
        before = image_parts(a.before/suffix)
        after = image_parts(a.after/'projects'/key/suite/'.build/mx251/latest')
        keil = keil_parts(a.references/'projects'/key/'keil-fixed')
        old = {f['name']: f['bytes'] for f in before['functions']}
        new = {f['name']: f['bytes'] for f in after['functions']}
        # ABI selection may rename a retained function. Count only the GC's
        # recorded removals, not a difference between linker symbol spellings.
        removed = sorted([dict(**f,bytes=old[f['symbol']]) for f in after['gc']['removedFunctions']],
                         key=lambda f:-f['bytes'])
        pairs=[]
        for name, size in new.items():
            c_name = (re.sub(r'^_+m251r4_', '', name) if re.match(r'^_+m251r4_', name)
                      else name.removeprefix('_')).upper()
            if c_name in keil['functions']:
                pairs.append(dict(name=c_name, mx=size, keil=keil['functions'][c_name], delta=size-keil['functions'][c_name]))
        rows.append(dict(id=key, afterSuite=suite, before=before, after=after, keil=keil,
            savedBytes=before['bytes']-after['bytes'], removedFunctions=removed,
            removedFunctionInstructionBytes=sum(f['bytes'] for f in removed),
            removedLibraryMembers=sorted(set(before['libraryMembers'])-set(after['libraryMembers'])),
            remainingFunctionPairs=sorted(pairs,key=lambda f:-f['delta'])))
    result=dict(status='FINAL_IMAGE_ATTRIBUTION', rows=rows, measurement=
        'Whole HEX payload bytes. MX function counts cover emitted instructions; other CSEG includes '
        'runtime, generated formatter and unowned code/tables. Keil function segments may include tables. '
        'Static byte counts do not measure runtime.')
    save(a.output,result)
    for row in rows:
        print(row['id'],row['before']['bytes'],'->',row['after']['bytes'],
              'removed application bytes',row['removedFunctionInstructionBytes'],
              'Keil %',round(100*row['after']['bytes']/row['keil']['bytes'],2))


if __name__ == '__main__': main()
