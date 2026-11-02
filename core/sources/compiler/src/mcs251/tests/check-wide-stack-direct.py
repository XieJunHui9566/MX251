"""Check stack address folding without changing observable transaction order."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
accepted=['d_select','d_loop','d_page','d_cast','d_end']
for model in ['small','large']:
 for opt in ['speed','size']:
  versions={}
  for label,compiler in [('current',a.sdcc)]+([('baseline',a.baseline)] if a.baseline else []):
   d=a.output_dir/f'{model}-{opt}-{label}';d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes())
   args=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-native-int-runtime','--mcs251-wide-backend','-c','test.c']
   q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
   asm=(d/'test.asm').read_text();assert re.findall(r'wide-mir accepted: (\w+)',asm)==accepted
   assert re.findall(r'wide-mir fallback: (\w+)',asm)==['d_back']
   traces={};direct=0
   for name in accepted:
    body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; mcs251 wide-mir',1)[0]
    traces[name]=re.findall(r'; wide memory (?:load|store) width=[124] volatile-bytes=[01]|\tecall\t\S+',body)
    for m in re.finditer(r'; wide memory (load|store) width=([124]) volatile-bytes=1\n; wide stack direct object=\d+ offset=\d+\n',body):
     op,width=m[1],int(m[2]);segment=body[m.end():];displacements=[]
     for ins in segment.splitlines():
      match=re.fullmatch(r'\tmov\t'+(r'a,(@spx(?:[+-]0x[0-9a-f]+)?)' if op=='load' else r'(@spx(?:[+-]0x[0-9a-f]+)?),a'),ins)
      if match:
       x=match[1][4:];displacements.append(int(x,16) if x else 0)
       if len(displacements)==width:break
     assert len(displacements)==width and displacements==list(range(displacements[0],displacements[0]+width)),(name,displacements)
     direct+=1
   if label=='current':assert direct==21,direct
   versions[label]=dict(transactions=traces,direct=direct)
  if a.baseline:assert versions['current']['transactions']==versions['baseline']['transactions']
  rows.append(dict(model=model,optimize=opt,accepted=accepted,fallback=['d_back'],versions=versions,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 4 configurations: stack transaction order and 21 direct accesses; no hardware claim')
