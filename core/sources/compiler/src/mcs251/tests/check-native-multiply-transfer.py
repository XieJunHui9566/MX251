import argparse, pathlib, subprocess, re, json
p=argparse.ArgumentParser();p.add_argument('--sdcc',required=True);p.add_argument('--source',required=True);p.add_argument('--output-dir',required=True)
a=p.parse_args();d=pathlib.Path(a.output_dir).resolve();d.mkdir(exist_ok=False);rows=[]
def check_preserved(body):
 # 对入口寄存器赋唯一标记，跟踪实际 push/pop 和寄存器复制；
 # 操作数快照的 pop 不是恢复原值，单纯数同名 push/pop 会误判。
 def registers(name):
  name={'a':'r11','acc':'r11','b':'r10'}.get(name,name)
  if re.fullmatch(r'ar[0-7]',name):name=name[1:]
  m=re.fullmatch(r'(r|wr|dr)(\d+)',name)
  if m:return ['r'+str(i) for i in range(int(m[2]),int(m[2])+{'r':1,'wr':2,'dr':4}[m[1]])]
  return [name] if re.fullmatch(r'[a-z]+',name) else []
 values={f'r{i}':f'entry-r{i}' for i in range(32)};stack=[];serial=0
 for line in body.splitlines():
  line=line.split(';')[0].strip().lower();m=re.match(r'(push|pop|mov|mul|xrl)\s+(.+)',line)
  if not m:continue
  op,args=m[1],m[2].split(',');dest=registers(args[0].strip());serial+=1
  if op=='push':
   assert dest,line
   stack.extend(values.get(x,'entry-'+x) for x in dest)
  elif op=='pop':
   assert len(stack)>=len(dest),(line,stack)
   for x in reversed(dest):values[x]=stack.pop()
  elif op=='mul':
   first=int(args[0].strip()[2:])&~3
   for i in range(first,first+4):values['r'+str(i)]=f'product-{serial}-{i}'
  elif op=='xrl':
   for x in dest:values[x]=f'xor-{serial}-{x}'
  elif op=='mov' and dest:
   src=registers(args[1].strip())
   incoming=[values.get(x,'entry-'+x) for x in src] if len(src)==len(dest) else [f'load-{serial}-{i}' for i in range(len(dest))]
   for x,v in zip(dest,incoming):values[x]=v
 assert not stack,stack
 for i in [8,9,*range(12,32)]:assert values['r'+str(i)]=='entry-r'+str(i),(i,values['r'+str(i)])
for model in ['small','large']:
 for auto in [False,True]:
  for speed in [False,True]:
   for peep in [False,True]:
    name=f'{model}-{auto}-{speed}-{peep}';out=d/name;out.mkdir()
    cmd=[str(pathlib.Path(a.sdcc).resolve()),'-mmcs251','--model-'+model,'--opt-code-speed' if speed else '--opt-code-size']+(['--stack-auto'] if auto else [])+([] if peep else ['--no-peep'])+['-c',str(pathlib.Path(a.source).resolve()),'-o','case.rel']
    r=subprocess.run(cmd,cwd=out,capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);assert not r.returncode,(name,r.stderr)
    asm=(out/'case.asm').read_text()
    for fn in ['ordinary_mul','saved_mul','irq_mul','irq_bank_mul','ordinary_chain','irq_plan_mul']:
     body=re.search(r'^_'+fn+r':\n(.*?)(?=^;-{20,}$|\Z)',asm,re.M|re.S).group(1)
     native=bool(re.search(r'\bmul\s+wr16,wr20',body))
     legacy=bool(re.search(r'\bmul\s+wr12,wr8',body))
     assert native or legacy,(name,fn,'missing native multiply')
     if fn=='saved_mul':assert legacy and not native,(name,fn,'callee-save fallback lost')
     if fn not in ['ordinary_mul','ordinary_chain']:
      check_preserved(body)
     rows.append(dict(configuration=name,function=fn,nativePlan=native,legacyPath=legacy,passed=True))
assert any(r['nativePlan'] for r in rows),'New multiply plan was not covered'
assert any(r['nativePlan'] and r['function']=='irq_plan_mul' for r in rows),'New plan ISR preservation was not covered'
(d/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'multiply transfer/context checks')
