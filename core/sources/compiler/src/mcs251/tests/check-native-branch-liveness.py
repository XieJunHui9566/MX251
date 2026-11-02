import argparse, pathlib, subprocess, re, json, tempfile
p=argparse.ArgumentParser();p.add_argument('--sdcc',required=True);p.add_argument('--source',required=True);p.add_argument('--output-dir');a=p.parse_args()
d=pathlib.Path(a.output_dir) if a.output_dir else pathlib.Path(tempfile.mkdtemp(prefix='mcs251-branch-'))
if a.output_dir:d.mkdir(exist_ok=False)
rows=[]
for model in ['small','large']:
 for stack in [False,True]:
  for speed in [False,True]:
   for peep in [False,True]:
    tag=f'{model}-{stack}-{speed}-{peep}'
    args=[a.sdcc,'-mmcs251','--model-'+model,'--opt-code-speed' if speed else '--opt-code-size']+(['--stack-auto'] if stack else [])+([] if peep else ['--no-peep'])+['-S',a.source,'-o',str(d/(tag+'.asm'))]
    r=subprocess.run(args,capture_output=True);(d/(tag+'.log')).write_bytes(r.stdout+r.stderr)
    assert r.returncode==0,r.stderr
    asm=(d/(tag+'.asm')).read_text();body=asm[asm.index('_receive:'):]
    counter=re.search(r'cjne\s+(r\d+),#0x20',body)
    if not counter:
        counter=re.search(r'mov\s+a,(r\d+)\s*\n\s*cjne\s+a,#0x20',body)
    assert counter and ('jne\t' in body or 'cjne\t' in body),tag+' did not exercise the conditional edge'
    initialized=bool(re.search(r'mov\s+'+counter.group(1)+r',\s*#0x00',body))
    rows.append(dict(name=tag,initialized=initialized,nativeJne=bool(re.search(r'^\s*jne\t',body,re.M)),args=args))
(d/'results.json').write_text(json.dumps(rows,indent=2))
assert all(r['initialized'] for r in rows), 'A live counter initialization was removed'
print('Native branch liveness:',len(rows),'passed; artifacts:',d)
