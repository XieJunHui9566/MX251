"""Ordinary-entry CALL coverage, physical frames, isolation and rejected ABIs."""
from pathlib import Path
import argparse,subprocess,re,json
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
T=Path(__file__).resolve().parent;a.output_dir.mkdir(exist_ok=False);rows=[]
def build(name,model,source,flags,compiler=None,opt='speed'):
 d=a.output_dir/name;d.mkdir();(d/'wide-ordinary-calls.h').write_bytes((T/'wide-ordinary-calls.h').read_bytes());(d/'test.c').write_text(source)
 cmd=[str((compiler or a.sdcc).resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,*flags,'-c','test.c']
 q=subprocess.run(cmd,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);(d/'command.json').write_text(json.dumps(cmd));assert q.returncode==0,(name,q.stderr)
 return (d/'test.asm').read_text(),(d/'test.rel').read_bytes()
for model in ['small','large']:
 for opt in ['speed','size']:
  name=model+'-'+opt;source=(T/'wide-ordinary-calls-callers.c').read_text()
  asm,obj=build(name,model,source,['--mcs251-wide-ordinary'],opt=opt)
  _,again=build(name+'-repeat',model,source,['--mcs251-wide-ordinary'],opt=opt);assert obj==again
  _,off=build(name+'-disabled',model,source,['--mcs251-wide-backend'],opt=opt)
  _,old=build(name+'-old',model,source,['--mcs251-wide-backend'],a.baseline,opt=opt);assert off==old
  accepted=re.findall(r'wide-mir accepted: (\w+)',asm);assert len(accepted)==12 and 'wide-mir fallback:' not in asm
  assert not re.search(r'\br(?:1[6-9]|2[0-9]|3[01])\b',asm)
  assert not re.search(r'\tejmp\t_(?!_wm_)\w+',asm),'No ordinary-entry tail jump may bypass ABI return/frame'
  parts={n:asm.split('wide-mir accepted: '+n+';',1)[1].split('; wide allocation selected=',1)[0] for n in accepted}
  assert '\tecall\t_recursive_value' in parts['recursive_value']
  assert '; wide stack call args=7 bytes=17' in parts['c_many']
  assert 'wide object a slot=0 size=4' in parts['c_alias'] and 'wide parameter address entry-offset=-6' in parts['c_alias']
  # 压力用例可能因已验证的活区间改进而不再溢出；下方仍逐槽检查
  # 所有实际 spill 与对象的边界，不能把“必须存在低效分配”当 ABI 契约。
  assert re.search(r'wide v\d+ .*width=4 reg=',parts['c_pressure'])
  assert '\tecall\t_old_float' in parts['forward_float'] and '\tecall\t___m251r4_native_float' in parts['forward_float']
  assert 'wide ordinary ABI2 return physical=4' in parts['forward_float']
  assert 'wide ordinary ABI call: args=3 stack=8' in parts['c_frame']
  for n,body in parts.items():
   frame=int(re.search(r'frame=(\d+)',body)[1]);occupied=set()
   for slot,size in re.findall(r'wide object \w+ slot=(\d+) size=(\d+)',body):
    slot=int(slot);size=int(size);region=set(range(slot,slot+size));assert slot+size<=frame and occupied.isdisjoint(region);occupied|=region
   for width,slot in re.findall(r'wide v\d+ \S+ width=(\d+) reg=-1 slot=(\d+)',body):
    slot=int(slot);width=int(width);assert slot+width<=frame and occupied.isdisjoint(range(slot,slot+width))
  # Live values must be saved, then the result reconstructed before restoration.
  body=parts['c_pair'];call=body.index('\tecall\t_old_pair');pop=body.index('\tpop\t',call)
  assert '\tpush\t' in body[:call] and 'wide ordinary result direct' in body[call:pop]
  assert re.search(r'\tmov\tr\d+,dpl',body[call:pop])
  rows.append(dict(model=model,opt=opt,accepted=accepted,deterministic=True,optInIsolation=True))
header='typedef unsigned long u32;\n'
negative={
 'runtime-float':'float f(float a,float b) __reentrant {return a+b;}',
 'boolean-return':'_Bool h(u32) __reentrant;u32 f(u32 a) __reentrant {return h(a)+a;}',
 'critical-callee':'u32 h(u32) __reentrant __critical;u32 f(u32 a) __reentrant {return h(a)+a;}',
 'banked-register-callee':'u32 h(u32) __reentrant __using(1);u32 f(u32 a) __reentrant {return h(a)+a;}',
 'varargs-callee':'u32 h(u32 a,...) __reentrant;u32 f(u32 a) __reentrant {return h(a,1UL)+a;}',
 'noreturn-callee':'_Noreturn void h(void) __reentrant;void f(void) __reentrant {h();}',
 'ninth-argument':'u32 h(u32,u32,u32,u32,u32,u32,u32,u32,u32) __reentrant;u32 f(u32 a) __reentrant {return h(a,a,a,a,a,a,a,a,a);}',
}
for model in ['small','large']:
 for name,source in negative.items():
  asm,obj=build(model+'-'+name,model,header+source,['--mcs251-wide-ordinary'])
  _,old=build(model+'-'+name+'-old',model,header+source,['--mcs251-wide-ordinary'],a.baseline)
  assert 'wide-mir accepted:' not in asm and obj==old,(model,name)
  rows.append(dict(model=model,boundary=name,fallbackIdentical=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ordinary CALL configurations/boundaries')
