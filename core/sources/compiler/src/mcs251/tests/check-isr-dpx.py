"""Separate DPTR/DPXL saves, old feedback compatibility and ISR address reuse."""
import argparse,json,os,re,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain.resolve());env['PATH']=str(a.toolchain.resolve()/'bin')+';'+env['PATH']
rows=[]
for model in ('small','large'):
 for opt in ('speed','size'):
  root=a.output/(model+'-'+opt);root.mkdir()
  flags=['-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto','--mcs251-wide-ordinary']
  def compile(source,tag,facts=None,compiler=a.sdcc,extra=None):
   (root/'test.c').write_text(source,newline='\n')
   args=[str(compiler.resolve()),*flags]
   if facts:
    (root/'effects.txt').write_text(facts,newline='\n');args+=['--mcs251-leaf-effects','effects.txt']
   r=subprocess.run([*args,'-c','test.c','-o',tag+'.rel'],cwd=root,env=dict(env,**(extra or {})),capture_output=True)
   (root/(tag+'.log')).write_bytes(r.stdout+r.stderr);assert r.returncode==0,(tag,r.stderr)
   text=(root/(tag+'.asm')).read_text(encoding='latin1')
   return text,(root/(tag+'.rel')).read_bytes()
  def saves(text):return set(re.findall(r'^\s*push\s+(dpl|dph|dpxl)\s*$',text,re.M))
  source='void helper(void); void irq(void) __interrupt(1) { helper(); }\n'
  for mask in range(4):
   asm,_=compile(source,'call-'+str(mask),f'MX251-CALL-CLOBBERS-3\nglobal _helper 2 00000c00 {mask}\n')
   expected=({'dpl','dph'} if mask&1 else set())|({'dpxl'} if mask&2 else set())
   assert saves(asm)==expected,(root,mask,saves(asm))
   if mask!=3:assert f'mx251-isr-dpx-call callee=_helper omitted={3^mask}' in asm
   rows.append(dict(model=model,opt=opt,kind='call',mask=mask,saves=sorted(expected)))
  for version,facts in [(1,'_helper'),(2,'global _helper')]:
   contract=f'MX251-CALL-CLOBBERS-{version}\n{facts} 2 00000c00\n'
   _,obj=compile(source,'old-'+str(version),contract)
   _,old=compile(source,'baseline-'+str(version),contract,compiler=a.baseline)
   assert obj==old,(root,'old feedback changed')
  for address,expected in [(0x82,{'dpl','dph'}),(0x84,{'dpxl'})]:
   asm,_=compile(f'__sfr __at({address}) special; void irq(void) __interrupt(1) {{ special=7; }}','direct-'+str(address))
   assert saves(asm)==expected,(root,address,saves(asm))
  source='''typedef unsigned char u8;
#define FAR(n) (*(volatile __xdata u8 *)(0x12000UL+(n)))
__xdata u8 buffer[8];
void irq(void) __interrupt(1) { FAR(0)=0x41; FAR(1)=(u8)buffer; FAR(2)=3; }
'''
  asm,_=compile(source,'reuse')
  old,_=compile(source,'reuse-baseline',compiler=a.baseline)
  count=lambda s:len(re.findall(r'^\s*mov\s+dpxl,',s,re.M))
  assert count(asm)==1 and count(old)==3,(root,count(asm),count(old))
  assert 'mx251-leaf-isr-saves' in asm,(root,'address reuse lost ISR save analysis')
  # A store through a bank-zero flat pointer may change S:84H itself.
  alias='''typedef unsigned char u8;
void probe(void) __interrupt(1) { *(volatile __xdata u8 *)0x84UL=1; *(volatile __xdata u8 *)0x85UL=2; }
'''
  asm,_=compile(alias,'alias')
  assert count(asm)==2,(root,'flat DPXL alias',count(asm))
  rows.append(dict(model=model,opt=opt,kind='address-reuse',before=count(old),after=count((root/'reuse.asm').read_text())))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=4,callMasks=16,oldFormatComparisons=8,directAliases=8,addressChecks=8,rows=rows),indent=2)+'\n')
print('PASS: 4 configurations, 16 call masks, 8 old-format comparisons, 8 direct aliases and 8 address checks')
