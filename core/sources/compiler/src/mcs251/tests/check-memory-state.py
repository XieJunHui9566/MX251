"""Memory-state contracts, ordered accesses, local machine barriers and opt-out."""
import argparse, json, os, re, subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','toolchain','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env['SDCC_HOME']=str(a.toolchain.resolve())
env['PATH']=str(a.toolchain.resolve()/'bin')+';'+env['PATH']
rows=[]
for model in ('small','large'):
 for opt in ('speed','size'):
  root=a.output/(model+'-'+opt);root.mkdir()
  flags=['-mmcs251','--model-'+model,'--opt-code-'+opt,'--std-sdcc11','--stack-auto',
         '--mcs251-wide-ordinary','--mcs251-ordered-asm','--mcs251-machine-ir',
         '--mcs251-ordered-asm-bits','88:ff']
  cap=['--mcs251-memory-state-bits','ba:80']
  def compile(source, compiler, extra, name, success=True):
   (root/'test.c').write_text(source,encoding='utf8',newline='\n')
   cmd=[str(compiler.resolve()),*flags,*extra,'-c','test.c','-o',name+'.rel']
   r=subprocess.run(cmd,cwd=root,env=env,capture_output=True)
   (root/(name+'.log')).write_bytes(r.stdout+r.stderr)
   assert (r.returncode==0)==success,(root,name,r.stderr)
   if not success:return '',b''
   return (root/(name+'.asm')).read_text(encoding='latin1'),(root/(name+'.rel')).read_bytes()
  header='volatile __data unsigned char cell;\n'
  for bit in ('0xba.7',):
   source=header+('unsigned long work(unsigned long x) { cell=1; __asm setb BIT __endasm; '
       'cell=2; x=(x<<3)^cell; __asm clr BIT __endasm; cell=3; return x+cell; }\n').replace('BIT',bit)
   name=bit.replace('.','-')
   text,obj=compile(source,a.sdcc,cap,name)
   assert 'wide-mir accepted: work' in text,(root,name)
   assert text.count('memory-state=1')==4,(root,name)
   assert '; mx251-machine-ir ' in text,(root,name)
   body=text[text.index('_work:'):]
   assert len(re.findall(r'\bsetb\s+'+re.escape(bit),body))==1
   assert len(re.findall(r'\bclr\s+'+re.escape(bit),body))==1
   assert body.index('setb\t'+bit)<body.index('clr\t'+bit)
   # Every volatile store/read survives, in order on either side of the gates.
   accesses=[x or y for x,y in re.findall(r'; wide symbol (store|load) _cell|\b(setb|clr)\s+'+re.escape(bit),body)]
   assert accesses==['store','setb','store','load','clr','store','load'],(root,accesses)
   _,off=compile(source,a.sdcc,[],name+'-off')
   _,old=compile(source,a.baseline,[],name+'-old')
   assert off==old,(root,'absent contract changed legacy output')
   rows.append(dict(model=model,optimization=opt,bit=bit,accepted=True,optOutMatches=True))
  for name,asm in [('bank','setb 0xd0.3'),('stack','setb 0x85.0'),('extram','setb 0x8e.1'),
                   ('mixed','setb 0xba.7\nmov r0,#1')]:
   source='void work(void) { __asm\n'+asm+'\n__endasm; }\n'
   text,obj=compile(source,a.sdcc,cap,name)
   assert 'wide-mir fallback: work' in text
   _,old=compile(source,a.baseline,[],name+'-old');assert obj==old
   rows.append(dict(model=model,optimization=opt,case=name,excludedMatches=True))
  source='''typedef unsigned char u8;
__sfr __at(0x99) sink;
volatile __bit ready;
volatile __data u8 index;
__data u8 buffer[8];
void work(void) {
 u8 i;
 __asm setb 0xba.7 __endasm;
 for(i=0;i<8;++i) buffer[i]=16;
 for(;;) if(ready) {ready=0; sink=buffer[index]; index=(index+1)&7;}
}
'''
  text,obj=compile(source,a.sdcc,cap,'byte-loop')
  assert 'mx251-body-choice function=_work selected=legacy' in text
  _,old=compile(source,a.baseline,[],'byte-loop-old');assert obj==old
  rows.append(dict(model=model,optimization=opt,case='byte-loop',selected='legacy',objectMatches=True))
  if model=='small' and opt=='speed':
   for i,bits in enumerate(('d0:08','85:01','87:01','ba:00','ba:80,ba:01','ba:80,')):
    compile('void work(void) {}',a.sdcc,['--mcs251-memory-state-bits',bits],'invalid-'+str(i),False)
   compile('void work(void) {}',a.sdcc,['--mcs251-ordered-asm-bits','ba:80'],'ordinary-mapping',False)
(a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n',newline='\n')
print('PASS',len(rows),'configurations; 7 invalid contracts rejected; opt-out/excluded REL match parent')
