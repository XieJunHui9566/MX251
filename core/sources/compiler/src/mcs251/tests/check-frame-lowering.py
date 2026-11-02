import argparse,pathlib,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--sdcc',required=True);p.add_argument('--source',required=True);p.add_argument('--output-dir',required=True);a=p.parse_args()
D=pathlib.Path(a.output_dir).resolve();D.mkdir(exist_ok=False);rows=[]
for model in ['small','large']:
 for auto in [False,True]:
  for speed in [False,True]:
   for peep in [False,True]:
    name=f'{model}-{auto}-{speed}-{peep}';d=D/name;d.mkdir()
    cmd=[str(pathlib.Path(a.sdcc).resolve()),'-mmcs251','--model-'+model,'--opt-code-speed' if speed else '--opt-code-size','--i-code-in-asm']+(['--stack-auto'] if auto else [])+([] if peep else ['--no-peep'])+['-c',str(pathlib.Path(a.source).resolve()),'-o','case.rel']
    r=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(r.stdout+r.stderr);assert r.returncode==0,(name,r.stderr)
    (d/'command.json').write_text(json.dumps(cmd,indent=2));asm=(d/'case.asm').read_text()
    for fn in ['frame_fields','frame_pressure','frame_chain','frame_loop']:
     body=re.search(r'^_'+fn+r':\n(.*?)(?=^;-{20,}$|\Z)',asm,re.M|re.S).group(1)
     count=len(re.findall(r'mcs251 frame (?:store|load)',body));assert count>0,(name,fn)
     # The memory operations in final iCode are typed frame views, not pointer temporaries.
     assert '__mcs251_frame_' in body,(name,fn)
     assert re.search(r'ecall\s+_modify_bytes',body) if fn!='frame_loop' else True
     rows.append(dict(configuration=name,function=fn,frameAccesses=count,passed=True))
(D/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'frame lowering compiler checks')
