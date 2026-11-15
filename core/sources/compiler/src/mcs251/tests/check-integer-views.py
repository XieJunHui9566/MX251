"""Compile mixed integer views and check the origin of object-memory facts."""
import argparse,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','support','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','MCS251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support.resolve()),PATH=str(a.support.resolve()/'bin')+os.pathsep+env['PATH'])
env.update(MX251_BODY_BACKEND_CHOICE='0',MX251_LINEAR_BACKEND_CHOICE='0')
def code(text):return [s for line in text.splitlines() if (s:=line.split(';',1)[0].strip())]
def boolean_result(text,abi,value):
    program=[];labels={}
    for line in code(text):
        if line.endswith(':'):labels[line[:-1]]=len(program)
        else:program.append(line.split(None,1))
    regs={'dpl':value,'r7':value};pc=0
    for _ in range(100):
        instruction=program[pc];pc+=1;op=instruction[0]
        if op=='eret':return regs['dpl' if abi==2 else 'r7']
        args=instruction[1].split(',')
        if op=='sjmp':pc=labels[args[0]];continue
        if op in ('jz','jnz'):
            if (regs.get('r11',0)==0)==(op=='jz'):pc=labels[args[0]]
            continue
        dest,source=args;dest='r11' if dest=='a' else dest
        rhs=int(source[1:],0) if source.startswith('#') else regs['r11' if source=='a' else source]
        if op=='mov':regs[dest]=rhs&255
        elif op=='add':regs[dest]=(regs[dest]+rhs)&255
        else:raise AssertionError(instruction)
    raise AssertionError('nonterminating boolean conversion')
rows=[]
for model in ('small','large'):
 for opt in ('speed','size'):
  for abi in (2,4):
   variants={}
   for mode,compiler in [('before',a.baseline),('after',a.sdcc),('disabled',a.sdcc)]:
    d=a.output/f'{model}-{opt}-abi{abi}'/mode;d.mkdir(parents=True)
    shutil.copy2(tests/'integer-views.c',d/'test.c')
    local=dict(env)
    if mode=='disabled':local.update(MX251_INTEGER_VIEW_COPIES='0',MX251_OBJECT_MEMORY_FACTS='0')
    cmd=[str(compiler.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--std-sdcc11',
      '--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata',
      '-DTEST_ABI='+str(abi),'-c','test.c']
    q=subprocess.run(cmd,cwd=d,env=local,capture_output=True,timeout=120)
    (d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
    variants[mode]=(d/'test.asm').read_text()
   assert code(variants['before'])==code(variants['disabled']),(model,opt,abi)
   funcs={m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=\S*_(view_\w+) .*?\n(.*?)'
        r'; mx251-function end schema=1',variants['after'],re.S)}
   assert len(funcs)==10,funcs.keys()
   assert 'mx251-object-memory schema=1' in funcs['view_object']
   for name in ('view_raw','view_fixed'):assert 'mx251-object-memory schema=1' not in funcs[name],name
   old={m[1]:m[2] for m in re.finditer(r'; mx251-function begin schema=1 symbol=\S*_(view_\w+) .*?\n(.*?)'
        r'; mx251-function end schema=1',variants['before'],re.S)}
   for value in range(256):
    assert boolean_result(funcs['view_bool'],abi,value)==(value+bool(value))&255
    assert boolean_result(old['view_bool'],abi,value)==(value+bool(value))&255
   assert '\tsra\t' in funcs['view_long'] and '\tsrl\t' in funcs['view_long']
   rows.append(dict(model=model,optimization=opt,abi=abi,status='PASS'))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2)+'\n')
print('PASS: 8 modes, disabled/parent equality, signed operations, boolean normalization and object-origin controls')
