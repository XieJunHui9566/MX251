"""ISR save coverage: calls, objects, flat stores, core aliases and exclusions."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain.resolve());env['PATH']=str(a.toolchain.resolve()/'bin')+';'+env['PATH']
header='''typedef unsigned char u8; typedef unsigned int u16;
volatile __data u8 index,value;
volatile __xdata u8 buffer[16];
__sfr __at(0x98) status;
__sbit __at(0x98) ready;
__sfr __at(0xba) mapping;
void helper(void); void unknown(void);
volatile __xdata u8 *pointer;
'''
cases={
 'known-call':('helper();',True,''),
 'unknown-call':('unknown();',False,''),
 'call-and-array':('helper(); buffer[index & 15] = value;',True,''),
 'indexed-array':('if (ready) {ready=0; buffer[index & 15]=value;}',True,''),
 'constant-store':('*(volatile __xdata u8 *)0x7efa12UL=0;',True,''),
 'constant-load':('value=*(volatile __xdata u8 *)0x7efa12UL;',True,''),
 'acc-bit':('if (*(volatile __xdata u8 *)0x7efa12UL & 1) value=3;',True,''),
 'conditional-store':('if (*(volatile __xdata u8 *)0x7efa12UL & 1) *(volatile __xdata u8 *)0x7efa12UL &= 0xfe;',True,''),
 'unknown-pointer':('*pointer=value;',False,''),
 'acc-alias':('*(volatile __xdata u8 *)0xe0UL=value;',False,''),
 'sp-alias':('*(volatile __xdata u8 *)0x81UL=value;',False,''),
 'narrow-pointer-cast':('*(volatile __data u8 *)buffer=value;',False,''),
 'mapping':('mapping |= 0x80;',False,''),
 'inline-asm':('__asm nop __endasm;',False,''),
 'other-bank':('value++;',False,' __using(1)'),
 'local-frame':('volatile u8 local[9]; local[0]=value; helper(); value=local[0];',False,''),
}
rows=[]
for model in ('small','large'):
 for opt in ('speed','size'):
  for name,(body,optimized,attribute) in cases.items():
   root=a.output/(model+'-'+opt+'-'+name);root.mkdir()
   (root/'test.c').write_text(header+'void irq(void) __interrupt(1)'+attribute+' {'+body+'}\n')
   (root/'effects.txt').write_text('MX251-CALL-CLOBBERS-2\nglobal _helper 2 003000c0\n')
   cmd=['-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto','--mcs251-wide-ordinary',
        '--mcs251-leaf-effects','effects.txt','-c','test.c']
   def run(compiler,extra,suffix):
    r=subprocess.run([str(compiler.resolve()),*cmd,'-o',suffix+'.rel'],cwd=root,env=dict(env,**extra),capture_output=True)
    (root/(suffix+'.log')).write_bytes(r.stdout+r.stderr);assert not r.returncode,(root,r.stderr)
    return (root/(suffix+'.asm')).read_text(encoding='latin1'),(root/(suffix+'.rel')).read_bytes()
   asm,obj=run(a.sdcc,{},'current')
   match=re.search(r'mx251-leaf-isr-saves .*?removed-bytes=(\d+) .*?stack-saved=(\d+)',asm)
   assert bool(match)==optimized,(root,asm)
   assert re.search(r'push\s+0xd1',asm) and re.search(r'pop\s+0xd1',asm),root
   if name=='known-call':
    assert not re.search(r'push\s+dr(?:0|8|12|16|24|28)\b',asm),root
    assert re.search(r'push\s+dr4\b',asm) and re.search(r'push\s+dr20\b',asm),root
    assert 'calls=1' in asm
   if not optimized:
    _,old=run(a.baseline,{},'baseline');assert obj==old,(root,'excluded case changed')
   _,disabled=run(a.sdcc,{'MX251_LEAF_ISR_SAVES':'0'},'disabled')
   _,old_disabled=run(a.baseline,{'MX251_LEAF_ISR_SAVES':'0'},'old-disabled')
   assert disabled==old_disabled,(root,'opt-out changed')
   rows.append(dict(model=model,optimization=opt,case=name,optimized=optimized,
                    savedBytes=int(match[1]) if match else 0,stackBytes=int(match[2]) if match else 0))
(a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n',newline='\n')
print('PASS',len(rows),'ISR configurations; excluded and disabled objects match the parent compiler')
