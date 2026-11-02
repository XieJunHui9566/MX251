#!/usr/bin/env python3
"""Flat generic-pointer codegen and volatile access boundaries; not a board test."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 d=a.output_dir/model;d.mkdir();(d/'generic.c').write_bytes(a.source.read_bytes())
 args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-speed','--mcs251-wide-backend','--mcs251-native-int-runtime','-c','generic.c']
 q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
 asm=(d/'generic.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm);fallback=re.findall(r'wide-mir fallback: (\w+)',asm)
 assert len(accepted)==13 and fallback==['g_volatile','g_code'],(accepted,fallback)
 vb=asm.split('wide-mir accepted: g_vbits;',1)[1].split('; mcs251 wide-mir',1)[0]
 assert vb.count('volatile-bytes=1')==2 and vb.count('\tmov\t@dr16,a')==4 and vb.count('\tmov\ta,@dr16')==4
 assert not re.search(r'mov\s+(wr\d+,@dr16|@dr16,wr\d+)',vb)
 assert re.search(r'wide-mir accepted: g_spill;.*frame=[1-9]',asm)
 ptr=asm.split('wide-mir accepted: g_ptrbits;',1)[1].split('; mcs251 wide-mir',1)[0]
 assert '\tanl\twr4,#0x00ff' in ptr
 rows.append(dict(model=model,args=args,accepted=accepted,fallback=fallback,volatileByteAccesses=8,pointerPaddingCanonicalized=True,hardwareExecuted=False))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS generic pointer transport, float copies, spills, volatile bytes and explicit fallback in both models')
