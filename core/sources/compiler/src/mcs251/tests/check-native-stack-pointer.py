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
    functions=re.findall(r'^u32 (union_\w+)\(',pathlib.Path(a.source).read_text(),re.M)
    for fn in functions:
     body=re.search(r'^_'+fn+r':\n(.*?)(?=^;-{20,}$|\Z)',asm,re.M|re.S).group(1)
     direct=len(re.findall(r'mcs251 (?:direct stack pointer|frame (?:load|store))',body))
     if fn in ['union_copy','union_partial','union_alias_call','union_repeated','union_control']:assert direct>0,(name,fn,body)
     if fn=='union_volatile':assert direct==0,(name,fn,'volatile object must use original path',body)
     if fn in ['union_volatile_field','union_volatile_pointer']:
      for chunk in re.findall(r';\s*mcs251 (?:direct stack pointer|frame) (?:store|load)[^\n]*\n(.*?);\s*mcs251 end (?:direct stack pointer|frame) access',body,re.S):
       accesses=[l for l in chunk.splitlines() if '@spx' in l]
       assert len(accesses)==4 and not any(re.search(r'\bwr\d+',l) for l in accesses),(name,fn,'volatile access width/count',accesses)
       def displacement(line):
        m=re.search(r'@spx(?:([+-])0x([0-9a-f]+))?',line)
        return (int(m[2],16)*(-1 if m[1]=='-' else 1)) if m[1] else 0
       addresses=[displacement(l) for l in accesses]
       assert addresses==[addresses[0]+i for i in range(4)],(name,fn,'volatile byte order',addresses)
     if fn=='union_alias_call':assert re.search(r'ecall\s+_modify_bytes',body),(name,fn)
     rows.append(dict(configuration=name,function=fn,passed=True,directAccesses=direct))
(d/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'stack pointer cases')
