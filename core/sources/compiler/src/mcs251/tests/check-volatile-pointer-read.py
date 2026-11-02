"""检查真正的指向对象读取，排除读取栈内指针参数产生的假阳性。"""
from pathlib import Path
import argparse,json,re,subprocess
a=argparse.ArgumentParser();a.add_argument('--sdcc',required=True);a.add_argument('--output-dir',required=True);args=a.parse_args()
D=Path(args.output_dir);D.mkdir(exist_ok=False);source=Path(__file__).with_name('volatile-pointer-read.c');rows=[]
for target in ['mcs251','mcs51']:
 for model in ['small','large']:
  for optimize in ['speed','size']:
   for peep in [False,True]:
    name=f'{target}-{model}-{optimize}-{peep}';d=D/name;d.mkdir()
    cmd=[args.sdcc,'-m'+target,'--model-'+model,'--opt-code-'+optimize,*([] if peep else ['--no-peep']),'-S',str(source),'-o','test.asm']
    p=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(p.stdout+p.stderr);assert not p.returncode,(name,p.stderr)
    asm=(d/'test.asm').read_text();data={}
    for function,expected in [('volatile_read',1),('ordinary_read',0)]:
     body=asm.split('_'+function+':',1)[1].split('\tret',1)[0].split('\teret',1)[0]
     store=re.search(r'^\s*mov\s+@r[01],#0x35\s*$',body,re.M);assert store,(name,function)
     loads=re.findall(r'^\s*mov\s+[^,\n]+,@r[01]\s*$',body[store.end():],re.M)
     data[function]=len(loads);assert len(loads)==expected,(name,function,len(loads))
    rows.append(dict(name=name,args=cmd,loadsAfterStore=data))
(D/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'compiler configurations with volatile and ordinary pointer controls')
