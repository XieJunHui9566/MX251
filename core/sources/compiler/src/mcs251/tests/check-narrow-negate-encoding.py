"""Check the added Source-mode costs against the actual assembler."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--host-cc', required=True)
p.add_argument('--assembler', required=True)
p.add_argument('--output-dir', required=True, type=Path)
a = p.parse_args()
a.output_dir.mkdir(parents=True, exist_ok=False)
root = Path(__file__).resolve().parent.parent
probe = a.output_dir / 'cost.c'
probe.write_text('''#include <stdio.h>
#include "backend_encoding.h"
int main(int argc,char **argv) {
  for(int i=1;i<argc;++i)printf("%u\\n",bc_instruction_bytes_mode(argv[i],strlen(argv[i]),NULL,NULL,1));
  return 0;
}
''')
exe = a.output_dir / 'cost.exe'
subprocess.run([a.host_cc, '-std=c99', '-I', str(root), str(probe), '-o', str(exe)], check=True)
cases = ['cjne a,#1,finish0', 'cjne a,0x82,finish1',
         'cjne r0,#1,finish2', 'cjne r7,#1,finish3',
         'cjne @r0,#1,finish4', 'cjne @r1,#1,finish5',
         'addc a,#1', 'addc a,0x82', 'addc a,r7', 'addc a,@r0',
         'subb a,#1', 'subb a,0x82', 'subb a,r7', 'subb a,@r1',
         'rrc a', 'rlc a', 'djnz b,finish16', 'djnz r0,finish17',
         'djnz r7,finish18', 'cpl a', 'pushw #_constant', 'div ab', 'div wr16,wr20']
q = subprocess.run([str(exe), *cases], check=True, capture_output=True, text=True)
costs = list(map(int, q.stdout.split()))
asm = '.module cost\n.globl _constant\n.area CSEG (CODE)\n'
for i, ins in enumerate(cases):
    asm += f'.globl begin{i}, finish{i}\nbegin{i}:\n\t{ins}\nfinish{i}:\n'
(a.output_dir / 'cost.asm').write_text(asm)
subprocess.run([a.assembler, '-plosgff', 'cost.rel', 'cost.asm'], cwd=a.output_dir, check=True)
rel = (a.output_dir / 'cost.rel').read_text()
for i, cost in enumerate(costs):
    start = int(re.search(rf'^S begin{i} Def([0-9A-F]+)', rel, re.M)[1], 16)
    end = int(re.search(rf'^S finish{i} Def([0-9A-F]+)', rel, re.M)[1], 16)
    assert cost == end - start, (cases[i], cost, end - start)
print('PASS', len(cases), 'instruction costs match assembled bytes')
