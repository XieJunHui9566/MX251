"""Inventory OMF251 libraries without claiming to import or link their code."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess


def inventory(path, lib251, output):
    data = path.read_bytes()
    counts, externals, modules, relocations = Counter(), set(), [], []
    offset = 0
    while offset < len(data):
        kind = data[offset]
        size = int.from_bytes(data[offset+1:offset+3], 'little')
        record = data[offset:offset+3+size]
        assert size and len(record) == size+3 and sum(record) % 256 == 0, offset
        body = record[3:-1]
        counts[f'{kind:02X}'] += 1
        if kind == 0xc0:
            modules.append(body[1:1+body[0]].decode('ascii'))
        if kind == 0x8a:
            # Each external has two 16-bit metadata fields and a counted name.
            pos = 0
            while pos < len(body):
                length = body[pos+4]
                name = body[pos+5:pos+5+length]
                assert len(name) == length, offset
                externals.add(name.decode('ascii'))
                pos += 5+length
            assert pos == len(body), offset
        if kind == 0x96:
            # Retain exact bytes. Relocation semantics are a later importer step.
            relocations.append(dict(offset=offset, payload=body.hex()))
        offset += len(record)
    listing = output/(path.name+'.publics.txt')
    run = subprocess.run([str(lib251), 'LIST', str(path), 'TO', str(listing), 'PUBLICS'],
                         capture_output=True)
    (output/(path.name+'.log')).write_bytes(run.stdout+run.stderr)
    assert run.returncode == 0, run.stdout
    publics = [s.strip() for s in listing.read_text('latin1').splitlines() if s.startswith('      ')]
    return dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                recordCounts=dict(sorted(counts.items())), modules=modules, publics=publics,
                externals=sorted(externals), relocations=relocations,
                linked=False, abiCompatibilityEstablished=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lib251', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('libraries', nargs='+', type=Path)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    rows = [inventory(f.resolve(), a.lib251, a.output) for f in a.libraries]
    (a.output/'inventory.json').write_text(json.dumps(rows, indent=2)+'\n')
    for row in rows:
        print(Path(row['path']).name, len(row['modules']), 'modules,',
              len(row['publics']), 'publics,', len(row['externals']), 'externals')
