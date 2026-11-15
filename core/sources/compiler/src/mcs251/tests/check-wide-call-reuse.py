"""Compile shared caller saves through ABI2/4, IRQ frames and ordinary entries."""
import argparse,json,os,re,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','support','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
sys.path.insert(0,str(tests.parents[5]/'tools/research'))
import call_save_reuse_fixture
fixture=a.output/'fixture';call_save_reuse_fixture.create(fixture)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support),MX251_WIDE_VALUE_LIMIT='1024',MX251_WIDE_NODE_LIMIT='2048')
env['PATH']=str(a.support/'bin')+os.pathsep+env['PATH']
rows=[]
for model in ('small','large'):
 for opt in ('speed','size'):
  d=a.output/(model+'-'+opt);d.mkdir()
  for name in ('tail-boundary.h','call-boundary-isr.c','isr-helper.c'):
   (d/name).write_text((fixture/name).read_text())
  (d/'ordinary.c').write_text('''typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
void context_record(u8,u16,u32);
u32 ordinary(u32 value,u8 sample,u16 word) {
 context_record(1,0x1234u,0x89abcdefUL);
 context_record(2,0x2345u,0x76543210UL);
 context_record(3,0x3456u,0xfedcba98UL);
 context_record(sample,word,value);return value;
}
''')
  for name in ('call-boundary-isr','isr-helper','ordinary'):
   cmd=[str(a.sdcc),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto',
        '--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata',
        '--mcs251-flat-code-reads','-c',name+'.c']
   q=subprocess.run(cmd,cwd=d,env=env,capture_output=True)
   (d/(name+'.log')).write_bytes(q.stdout+q.stderr);assert not q.returncode,(d,name,q.stderr)
  count=0
  for name in ('call-boundary-isr','ordinary'):
   text=(d/(name+'.asm')).read_text()
   reuse=re.findall(r'; wide call-save reuse bridges=(\d+) instructions=(\d+) weighted=(\d+)',text)
   assert reuse,(model,opt,name)
   count+=sum(int(r[1]) for r in reuse)
   if name=='call-boundary-isr':
    assert re.search(r'mcs251 wide-mir accepted: context_irq;',text)
    assert re.search(r'mcs251 wide-mir accepted: context_leaf_irq;',text)
    assert '__m251r4_context_native' in text
    framed=text.split('; mcs251 wide-mir accepted: context_leaf_irq;',1)[1].split('; mcs251 wide-mir accepted:',1)[0]
    assert '; wide call-save reuse bridges=' in framed,'ABI4 setup-overwrite coverage'
  rows.append(dict(model=model,opt=opt,removedInstructions=count))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2)+'\n')
print('PASS',rows)
