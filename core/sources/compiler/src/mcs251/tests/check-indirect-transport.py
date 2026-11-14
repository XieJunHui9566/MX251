"""Check that callback argument preparation preserves the chosen call target.

Run callback_fixture.py contracts first. Inspect every emitted instruction in
the target-to-call interval with the backend's instruction effect model.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--contracts', type=Path, required=True)
p.add_argument('--gcc', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = a.output / 'effects.c'
source.write_text('''#include <stdint.h>
#include <stdio.h>
#include "instruction_effects.h"
unsigned mcs251SphAddress=0xbe;
int main(int argc,char **argv) {
    char text[65536];size_t n=fread(text,1,sizeof text,stdin);
    unsigned reg=(unsigned)strtoul(argv[1],0,10);
    mx251_effects e=mx251TextEffects(text,n);
    printf("%u %08lx\\n",e.unknown,(unsigned long)e.writes);
    return e.unknown || (e.writes&mcs251RegisterBytes(reg,4));
}
''')
exe = a.output / 'effects.exe'
build = subprocess.run([str(a.gcc), '-std=c99', '-O2', '-I', str(Path(__file__).resolve().parents[1]),
                        str(source), '-o', str(exe)], capture_output=True)
assert not build.returncode, build.stderr
pattern = re.compile(r'; wide indirect target reg=(\d+) transport=(\w+)\n(.*?)'
                     r'; wide indirect call abi=(\d+)\n\s*ecall\s+@dr(\d+)', re.S)
rows, modes = [], Counter()
for path in sorted(a.contracts.glob('*/*.asm')):
    text = path.read_text()
    matches = list(pattern.finditer(text))
    assert len(matches) == text.count('; wide indirect call'), path
    for match in matches:
        reg, mode, setup, abi, target = match.groups()
        assert reg == target and not re.search(r'\bpush\s+dr20\b', setup), path
        result = subprocess.run([str(exe), reg], input=setup.encode(), capture_output=True)
        assert not result.returncode, (path, match.group(), result.stdout, result.stderr)
        modes[mode] += 1
        rows.append(dict(file=str(path), reg=int(reg), mode=mode, abi=int(abi),
                         setupWrites=result.stdout.decode().strip().split()[1]))
assert modes['resident'] and modes['snapshot'], modes
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', modes=dict(modes),
    calls=rows, targetPreserved=True, targetStackBytes=0), indent=2))
print(f'PASS: {len(rows)} indirect sites, resident and snapshot targets preserved by argument setup')
