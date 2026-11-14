"""Link local/external indexed symbols at nonzero region-00 addresses."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('assembler','linker','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
def run(cmd,d):
 q=subprocess.run(list(map(str,cmd)),cwd=d,capture_output=True)
 (d/(Path(cmd[0]).stem+'.log')).open('ab').write(q.stdout+q.stderr)
 assert not q.returncode,(d,q.stdout,q.stderr)
def image(path):
 data={};base=0
 for line in path.read_text().splitlines():
  b=bytes.fromhex(line[1:]);assert sum(b)&255==0
  if b[3]==4:base=int.from_bytes(b[4:6],'big')<<16
  elif b[3]==0:
   address=base+int.from_bytes(b[1:3],'big')
   data.update({address+i:v for i,v in enumerate(b[4:4+b[0]])})
 return data
rows=[]
for mode in ('source','binary'):
 for base in (0x1234,0x7ff8,0x8000,0xff00):
  d=a.output/f'{mode}-{base:04x}';d.mkdir()
  (d/'use.asm').write_text(f'''.module use
.{mode}
.globl _external
.area LOCAL (XDATA)
.ds 3
_local: .ds 16
.area TEST (CODE)
mov r7,@wr16+(_local+5)
mov @wr30+(_local+1),r8
mov wr24,@dr28+(_external+3)
mov @dr16+(_external-2),wr6
mov r3,@wr4-7
''')
  (d/'data.asm').write_text('.module data\n.globl _external\n.area EXTSEG (XDATA)\n.ds 2\n_external: .ds 16\n')
  for name in ('use','data'):run([a.assembler,'-plosgff',name+'.asm'],d)
  (d/'link.lk').write_text(f'-mwx\n-i linked.ihx\n-b TEST = 0xff0000\n-b LOCAL = {base:#x}\n-b EXTSEG = {base+0x40:#x}\nuse.rel\ndata.rel\n')
  run([a.linker,'-nf','link.lk'],d)
  # Source-map opcodes and physical-register fields from the ISA matrix.
  forms=[(0x09,0x78,base+8),(0x19,0x8f,base+4),(0x69,0xc7,base+0x45),
         (0x79,0x34,base+0x40),(0x09,0x32,0xfff9)]
  expected=b''.join((b'\xa5' if mode=='binary' else b'')+bytes([op,regs])+disp.to_bytes(2,'big')
                    for op,regs,disp in forms)
  memory=image(d/'linked.ihx');actual=bytes(memory[0xff0000+i] for i in range(len(expected)))
  assert actual==expected,(d,actual.hex(),expected.hex())
  rows.append(dict(mode=mode,base=base,expected=expected.hex(),actual=actual.hex()))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2))
print('PASS: 8 linked images; local/external WR/DR indexed relocations and unchanged numeric displacement')
