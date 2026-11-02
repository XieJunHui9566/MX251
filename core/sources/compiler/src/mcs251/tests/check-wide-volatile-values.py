#!/usr/bin/env python3
"""Check observable volatile transactions, byte ordering, alias calls and fallback."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
expected={
 'v_sequence':['S4','L4','S4','L4','S4','L4'],
 'v_twice':['S4','L4','L4'], 'v_short':['S2','L2','S2','L2'],
 'v_change':['L4','S4','L4'], 'v_alias':['S4','L4','CALL','L4'],
 'v_roundtrip':['S4','L4','L1','S1','L4','S4','L4'],
 'v_float_bits':['S4','L4','S4','L4'],
 'v_branch':['S4','L1','S1','L1','S1','L4'], 'v_loop':['S4','L4','L1','S1','L4']}
source=a.source.read_text(encoding='utf8')
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir();(d/'probe.c').write_text(source,encoding='utf8')
  flags=['-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-int-runtime']
  args=[str(a.sdcc.resolve()),*flags,'--mcs251-wide-backend','--dump-i-code','-c','probe.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  asm=(d/'probe.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert accepted==list(expected) and re.findall(r'wide-mir fallback: (\w+)',asm)==['v_signed'],accepted
  checked=[]
  for name,trace in expected.items():
   body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
   tokens=re.findall(r'; wide memory (load|store) width=([124]) volatile-bytes=(\d)|\tecall\t(___m251r4_v_change)',body)
   actual=['CALL' if callee else ('L' if op=='load' else 'S')+width for op,width,vol,callee in tokens]
   assert actual==trace,(name,actual,trace)
   assert all(callee or vol=='1' for op,width,vol,callee in tokens)
   count=0
   matches=list(re.finditer(r'; wide memory (load|store) width=([124]) volatile-bytes=1\n',body))
   for i,m in enumerate(matches):
    op=m[1];width=int(m[2]);segment=body[m.end():matches[i+1].start() if i+1<len(matches) else len(body)]
    instructions=[line.strip() for line in segment.splitlines() if line.startswith('\t')]
    accesses=[];increments=0
    if segment.startswith('; wide stack direct object='):
     displacements=[]
     for ins in instructions:
      # 原生字节栈访问可直接使用 R0..R15；仍核对事务数和递增地址。
      byte_register=r'(?:a|r(?:[0-9]|1[0-5]))'
      maddr=re.fullmatch(r'mov\t'+(byte_register+r',(@spx(?:[+-]0x[0-9a-f]+)?)' if op=='load' else r'(@spx(?:[+-]0x[0-9a-f]+)?),'+byte_register),ins)
      if maddr:
       address=maddr[1][4:];displacements.append(int(address,16) if address else 0)
       if len(displacements)==width:break
     assert len(displacements)==width and displacements==list(range(displacements[0],displacements[0]+width)),(name,op,displacements)
     count+=width
     continue
    for ins in instructions:
     if '@dr16' in ins:
      assert ins==('mov\ta,@dr16' if op=='load' else 'mov\t@dr16,a'),(name,ins)
      accesses.append(ins)
      if len(accesses)==width:break
     if ins=='inc\tdr16':increments+=1
    assert len(accesses)==width and increments==width-1,(name,op,width,accesses,increments)
    count+=width
   if name=='v_alias':assert 'ejmp\t___m251r4_v_change' not in body
   checked.append(dict(function=name,staticTransactions=actual,byteAccesses=count,addressIncrementOrder=True))
  # 有符号转换运行库仍须整体回退；字段地址常量现在进入上面的正例。
  negative=source.split('/*')[0]+source[source.index('u32 v_signed('):]
  rels=[]
  for wide in [False,True]:
   f=d/('fallback-'+str(wide));f.mkdir();(f/'probe.c').write_text(negative)
   command=[str(a.sdcc.resolve()),*flags,*(['--mcs251-wide-backend'] if wide else []),'-c','probe.c']
   q=subprocess.run(command,cwd=f,capture_output=True);(f/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
   rels.append((f/'probe.rel').read_bytes())
  assert rels[0]==rels[1]
  rows.append(dict(model=model,optimize=opt,accepted=accepted,transactions=checked,fallbackIdentical=True,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 4 configurations: volatile accesses, byte ordering, alias call and conservative fallback')
