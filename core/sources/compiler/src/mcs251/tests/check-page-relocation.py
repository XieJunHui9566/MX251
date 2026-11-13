"""Exact encodings plus linked ACALL/AJMP at non-page-aligned section bases."""
import argparse,json,re,shutil,subprocess
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--assembler',type=Path,required=True);p.add_argument('--linker',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parents[3]/'sdas/as251/tests'
assert list(tests.glob('*.expected'))
def run(root,label,args):
 r=subprocess.run([str(x) for x in args],cwd=root,capture_output=True)
 (root/(label+'.log')).write_bytes(r.stdout+r.stderr)
 return r.returncode
results=[]
for expected in sorted(tests.glob('*.expected')):
 root=a.output/expected.stem;root.mkdir();shutil.copy2(expected.with_suffix('.asm'),root/'test.asm')
 assert run(root,'assemble',[a.assembler,'-plosg','test.asm'])==0,expected.stem
 encoded=[]
 for line in (root/'test.lst').read_text().splitlines():
  m=re.match(r'^\s*[0-9a-fA-F]{4,8} ([0-9a-fA-F]{2}(?: [0-9a-fA-F]{2})*)(?=\s{2,})',line)
  if m:encoded.extend(m[1].lower().split())
 assert encoded==expected.read_text().lower().split(),expected.stem
 results.append(dict(name=expected.stem,status='PASS'))
cases=[('relocated-into-page',0xff0100,0x700,0x800,True),
       ('relocated-out-of-page',0xff0100,0x600,0x700,False),
       ('next-instruction-page',0xff0000,0x7fe,0x802,True)]
for name,base,site,target,ok in cases:
 for op,opcode in [('acall',0x11),('ajmp',1)]:
  root=a.output/(name+'-'+op);root.mkdir()
  source=f'.module page\n.area CSEG (REL,CON,CODE)\n.ds {site}\n{op} destination\n.ds {target-site-2}\ndestination:\nret\n'
  (root/'test.asm').write_text(source)
  assert run(root,'assemble',[a.assembler,'-plosgff','test.rel','test.asm'])==0,name
  rc=run(root,'link',[a.linker,'-n','-i','-m','-b',f'CSEG={base:#x}','linked','test.rel'])
  assert (rc==0)==ok,(name,op,rc)
  if ok:
   memory={};high=0
   for line in (root/'linked.ihx').read_text().splitlines():
    b=bytes.fromhex(line[1:])
    if b[3]==4:high=int.from_bytes(b[4:6],'big')<<16
    elif b[3]==0:memory.update({high+int.from_bytes(b[1:3],'big')+i:x for i,x in enumerate(b[4:-1])})
   dest=base+target;address=base+site
   assert memory[address]==opcode|((dest>>3)&0xe0) and memory[address+1]==dest&255
  else:assert 'Page' in (root/'link.log').read_text(errors='replace') or 'page' in (root/'link.log').read_text(errors='replace')
  results.append(dict(name=name+'-'+op,status='PASS',valid=ok))
# Mix ordinary and escaped relocation records at every byte alignment. The
# five-byte ACALL relocation used to reserve four bytes and overwrite the T
# record's high address byte when the relocation buffer was almost full.
root=a.output/'mixed-relocation-records';root.mkdir()
body=[]
for padding in range(16):
 body+=['nop']*padding
 body+=['sub wr20,wr20','mov (index+0),wr20','mov a,#(message>>16)','push acc','pushw #message','ecall page_target','sub spx,#3','acall far_target']
(root/'test.asm').write_text('.module mixed\n.globl message,far_target,page_target\n.area DSEG (DATA)\nindex:\n.ds 2\n.area CSEG (REL,CON,CODE)\n'+'\n'.join(body)+'\n')
(root/'targets.asm').write_text('.module targets\n.globl message,far_target,page_target\n.area CSEG (REL,CON,CODE)\nfar_target:\neret\npage_target:\nret\nmessage:\n.db 0\n')
for unit in ('test','targets'):
 assert run(root,'assemble-'+unit,[a.assembler,'-plosgff',unit+'.rel',unit+'.asm'])==0
assert run(root,'link',[a.linker,'-n','-i','-m','-b','CSEG=0xff0100','linked','test.rel','targets.rel'])==0
for line in (root/'test.rel').read_text().splitlines():
 if line.startswith('T '):assert int.from_bytes(bytes.fromhex(line[2:])[:3],'big')<1024
results.append(dict(name='mixed-relocation-records',status='PASS'))
(a.output/'result.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS',len(results),'encoding/relocation cases')
