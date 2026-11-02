"""离线访问宽度/次序检查；数据正确性另由实板及独立字节模型验证。"""
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes())
 args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-wide-backend','--mcs251-native-int-runtime','-c','test.c']
 r=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(r.stdout+r.stderr);assert not r.returncode
 asm=(d/'test.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+).*frame=(\d+)',asm)
 assert len(accepted)==28 and 'wide-mir fallback' not in asm
 def body(n):return asm.split('wide-mir accepted: '+n+';',1)[1].split('; mcs251 wide-mir',1)[0]
 for name,width in [('volatile32',4),('volatile16',2),('volatile8',1)]:
  b=body(name);ops=re.findall(r'; wide memory (\w+) width=(\d+) volatile-bytes=(\d+)',b)
  assert ops==[('load',str(width),'1'),('store',str(width),'1'),('load',str(width),'1')],(name,ops)
  assert b.count('\tmov\ta,@dr16')==2*width and b.count('\tmov\t@dr16,a')==width
  assert not re.search(r'mov\s+(wr\d+,@dr16|@dr16,wr\d+)',b)
 # Address folding can reduce the frame; keep a real spill fixture and verify
 # that every selected spill slot is still inside the declared frame.
 frame=int(dict(accepted)['pointer_spill']);assert 0<frame<=80
 slots=re.findall(r'width=(\d+) reg=-1 slot=(\d+)',body('pointer_spill'))
 assert slots and all(0<=int(s) and int(s)+int(w)<=frame for w,s in slots)
 assert re.search(r'mov\s+a,#\(\(_arena\+0\)>>16\)',body('global_address'))
 assert '\tanl\twr4,#0x00ff' in body('ptrbits') and '\tejmp\t___m251r4_pair32' in body('swap32')
 rows.append(dict(model=model,accepted=accepted,args=args,volatileByteOrderPreserved=True,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 28 memory/ABI functions and volatile byte accesses in two models')
