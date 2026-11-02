import argparse, pathlib, subprocess, re, json
p=argparse.ArgumentParser();p.add_argument('--sdcc',required=True);p.add_argument('--source',required=True);p.add_argument('--output-dir',required=True)
a=p.parse_args();d=pathlib.Path(a.output_dir).resolve();d.mkdir(exist_ok=False);rows=[]
for model in ['small','large']:
 for auto in [False,True]:
  for speed in [False,True]:
   for peep in [False,True]:
    name=f'{model}-{auto}-{speed}-{peep}';out=d/name;out.mkdir()
    cmd=[str(pathlib.Path(a.sdcc).resolve()),'-mmcs251','--model-'+model,'--opt-code-speed' if speed else '--opt-code-size']+(['--stack-auto'] if auto else [])+([] if peep else ['--no-peep'])+['-c',str(pathlib.Path(a.source).resolve()),'-o','case.rel']
    r=subprocess.run(cmd,cwd=out,capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr)
    (out/'command.json').write_text(json.dumps(cmd,indent=2));assert not r.returncode,(name,r.stderr)
    asm=(out/'case.asm').read_text()
    for fn in ['stack_load','stack_store','stack_truncate','stack_cross_page','stack_volatile','stack_volatile32']:
     body=re.search(r'^_'+fn+r':\n(.*?)(?=^;-{20,}$|\Z)',asm,re.M|re.S).group(1)
     lines=[l.split(';')[0].strip() for l in body.splitlines() if '@spx' in l.split(';')[0]]
     loads=[l for l in lines if re.search(r'\bmov\s+wr\d+,@spx',l)]
     stores=[l for l in lines if re.search(r'\bmov\s+@spx[^,]*,wr\d+',l)]
     if fn.startswith('stack_volatile'):
      n=4 if fn.endswith('32') else 2
      # 非易失 marker 在默认模型下也可能被保存到栈；只约束 value 的地址。
      assert not stores,(name,fn,'volatile store widened',lines)
      def displacement(line):
       m=re.search(r'@spx(?:([+-])0x([0-9a-f]+))?',line)
       return (int(m[2],16)*(-1 if m[1]=='-' else 1)) if m[1] else 0
      written={displacement(l) for l in lines if re.search(r'\bmov\s+@spx[^,]*,a',l)}
      assert all(not (written & {displacement(l),displacement(l)+1}) for l in loads),(name,fn,'volatile load widened',lines)
      assert sum(bool(re.search(r'\bmov\s+a,@spx',l)) for l in lines)==n,(name,fn,lines)
      assert sum(bool(re.search(r'\bmov\s+@spx[^,]*,a',l)) for l in lines)==n,(name,fn,lines)
      writes=[displacement(l) for l in lines if re.search(r'\bmov\s+@spx[^,]*,a',l)]
      reads=[displacement(l) for l in lines if re.search(r'\bmov\s+a,@spx',l)]
      assert reads==writes==[writes[0]-i for i in range(n)],(name,fn,'volatile access order changed',lines)
     elif fn=='stack_store':assert len(stores)>=2,(name,fn,lines)
     elif fn=='stack_load':assert len(loads)>=2,(name,fn,lines)
     elif fn=='stack_truncate':assert len(loads)>=1,(name,fn,lines)
     elif fn=='stack_cross_page':
      assert loads and stores and any(re.search(r'@spx-0x01[0-9a-f]{2}',l) for l in loads),(name,fn,lines)
     rows.append(dict(configuration=name,function=fn,passed=True,wordLoads=len(loads),wordStores=len(stores)))
(d/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'stack transfer checks')
