"""Verify real assembler offsets, conservative bounds and fixed switch entries."""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False)
def check_listing(asm_path):
 lines=asm_path.read_text().splitlines();listing={};labels={};short=[]
 for line in asm_path.with_suffix('.lst').read_text().splitlines():
  if len(line)<39 or not re.fullmatch('[0-9A-F]{6}',line[6:12]):continue
  m=re.search(r'(\d+)\s*$',line[32:39])
  if not m:continue
  n=int(m[1]);body=line[39:].lstrip(' ');assert body.strip()==lines[n-1].strip(),(n,body,lines[n-1])
  addr=int(line[6:12],16);data=bytes.fromhex(' '.join(re.findall('[0-9A-F]{2}',line[12:29])))
  listing[n]=(addr,data)
  if body.endswith(':'):labels[body[:-1]]=addr
  if body.startswith('\t') and data:assert len(data)<=6,(body,len(data))
 for n,line in enumerate(lines,1):
  m=re.match(r'; wide short branch direction=(forward|backward) bound=(\d+) kind=(conditional|goto)',line)
  if m:
   addr,data=listing[n+1];ins,target=lines[n].strip().split();assert ins in ['je','jne','jc','jnc','jg','jle','jsl','jsge','jsg','jsle','sjmp']
   delta=labels[target]-addr-len(data);assert -128<=delta<=127
   assert (data[-1] if data[-1]<128 else data[-1]-256)==delta,(target,delta,data.hex())
   bound=int(m[2]);assert (0<=delta<=bound<=127) if m[1]=='forward' else (-128<=-bound<=delta<0)
   short.append(dict(line=n+1,op=ins,displacement=delta,provenBound=bound,direction=m[1]))
 tables=[]
 for n,line in enumerate(lines):
  m=re.match(r'; wide jump table entries=(\d+)',line)
  if not m:continue
  j=n+1
  while lines[j]!='\tejmp\t@dr16':j+=1
  assert lines[j+1].endswith(':');base=listing[j+2][0];count=int(m[1])
  for k in range(count):
   row=j+3+k;addr,data=listing[row];assert lines[row-1].startswith('\tejmp\t__wm_') and len(data)==4 and data[0]==0x8a and addr==base+4*k
  tables.append(count)
 return dict(shortBranches=short,tableEntries=tables)
rows=[]
for model in ['small','large']:
 for opt in ['speed','size']:
  saved=[]
  for label,compiler in [('old',a.baseline),('new',a.sdcc)]:
   d=a.output_dir/(model+'-'+opt+'-'+label);d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes());args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi','--mcs251-wide-backend','--opt-code-'+opt,'-c','test.c'];q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr;saved.append(d)
  old,new=[(d/'test.asm').read_text() for d in saved]
  assert len(re.findall('wide-mir accepted:',new))==6 and 'wide-mir fallback' not in new
  body=lambda s:[x for x in s.splitlines() if x.startswith('\t') and not re.match(r'\t(?:j\w+|ejmp|sjmp)\t',x)]
  assert body(old)==body(new),'non-branch operations/order/registers changed unexpectedly'
  checked=check_listing(saved[1]/'test.asm');assert checked['tableEntries']==[256] and any(x['direction']=='backward' for x in checked['shortBranches']) and any(x['direction']=='forward' for x in checked['shortBranches'])
  for name in ['far_forward','far_backward']:
   part=new.split('wide-mir accepted: '+name+';',1)[1].split('; wide allocation ',1)[0];assert '\tejmp\t__wm_' in part and part.count('\tecall\t')>=18
  first=(saved[1]/'test.rel').read_bytes();q=subprocess.run(args,cwd=saved[1],capture_output=True);(saved[1]/'determinism.log').write_bytes(q.stdout+q.stderr);assert not q.returncode and first==(saved[1]/'test.rel').read_bytes()
  rows.append(dict(model=model,opt=opt,nonBranchInstructionsUnchanged=True,deterministic=True,**checked))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS4 layouts: actual relative displacements,6-byte bounds,256 fixed entries, unchanged non-branch instructions')
