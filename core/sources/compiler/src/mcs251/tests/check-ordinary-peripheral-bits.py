"""Ordinary bit admission, exact byte.bit identity, access order and opt-out."""
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
         '--mcs251-wide-ordinary','--mcs251-machine-ir']
  cap=['--mcs251-ordered-asm','--mcs251-ordered-asm-bits','9a:ff,af:ff,cd:c0,ce:ff']
  def compile(source,compiler,extra,name):
   (root/'test.c').write_text(source,encoding='utf8',newline='\n')
   r=subprocess.run([str(compiler.resolve()),*flags,*extra,'-c','test.c','-o',name+'.rel'],
                    cwd=root,env=env,capture_output=True)
   (root/(name+'.log')).write_bytes(r.stdout+r.stderr)
   assert r.returncode==0,(root,name,r.stderr)
   return (root/(name+'.asm')).read_text(encoding='latin1'),(root/(name+'.rel')).read_bytes()
  source='''volatile __data unsigned char cell;
unsigned long work(unsigned long x) {
 cell=1;
 __asm
  clr 0xaf.7
  setb 0x9a.3
 __endasm;
 x=(x<<3)^cell; cell=2;
 __asm
  cpl 0xce.3
  setb 0xaf.7
  setb 0xaf.7
 __endasm;
 return x+cell;
}
'''
  text,_=compile(source,a.sdcc,cap,'bits')
  assert 'wide-mir accepted: work' in text,root
  body=text[text.index('_work:'):]
  accesses=[x or y for x,y in re.findall(r'; wide symbol (store|load) _cell|\b((?:clr|setb|cpl)\s+0x[0-9a-f]{2}\.[0-7])',body)]
  accesses=[' '.join(x.split()) for x in accesses]
  assert accesses==['store','clr 0xaf.7','setb 0x9a.3','load','store','cpl 0xce.3','setb 0xaf.7','setb 0xaf.7','load'],(root,accesses)
  for suffix,options in [('off',[]),('absent',['--mcs251-ordered-asm'])]:
   _,off=compile(source,a.sdcc,options,suffix)
   _,old=compile(source,a.baseline,options,suffix+'-old')
   assert off==old,(root,suffix)
  for name,asm in [('bank','setb 0xd0.3'),('stack','setb 0x85.0'),
                   ('mapping','setb 0x8e.1'),('mixed','setb 0xaf.7\nmov r0,#1')]:
   source='void work(void) { __asm\n'+asm+'\n__endasm; }\n'
   text,obj=compile(source,a.sdcc,cap,name)
   assert 'wide-mir fallback: work' in text,(root,name)
   _,old=compile(source,a.baseline,cap,name+'-old')
   assert obj==old,(root,name)
  source='''void output(unsigned char a,unsigned char b) __sdcccall(4);
volatile __data unsigned char index;
void poll(void) {
 unsigned char n=0;
 __asm clr 0xaf.7 __endasm;
 while(index && n<32) { output(3,n); ++n; }
 __asm setb 0xaf.7 __endasm;
}
'''
  text,_=compile(source,a.sdcc,cap,'native-call')
  assert 'mx251-body-choice function=_poll selected=' in text,(root,'native-call comparison missing')
  source=source.replace('output(3,n);', '''switch(n&7) {
 case 0: index=3; break; case 1: index=17; break;
 case 2: index=23; break; case 3: index=41; break;
 case 4: index=59; break; case 5: index=61; break;
 case 6: index=79; break; default: index=89; break;
 } output(3,n);''')
  text,_=compile(source,a.sdcc,cap,'switch-native-call')
  assert 'mx251-body-choice function=_poll selected=' in text,(root,'switch comparison missing')
  assert re.search(r'mx251-backend-choice function=_poll .*unknown=0/0',text),(root,'switch cost missing')
  source='''__sbit __at(0xd0) parity;
static inline unsigned char bitwrite(unsigned char state) {
 if(state) { __asm setb 0x9a.3 __endasm; }
 else { __asm clr 0x9a.3 __endasm; }
 return state;
}
unsigned char work(unsigned char x) { if(x) return bitwrite(!parity); return 0; }
'''
  text,_=compile(source,a.sdcc,cap,'bit-temporary')
  assert re.search(r'mx251-backend-choice function=_work .*unknown=0/0',text),(root,'bit temporary cost missing')
  rows.append(dict(model=model,optimization=opt,accessOrder=True,optOutMatches=True,excludedMatches=True,nativeCallCompared=True))
(a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n',newline='\n')
print('PASS: 4 configurations; exact bit identity/access order, opt-out and core/unknown exclusions')
