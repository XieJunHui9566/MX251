"""Translate the emitted native arithmetic core into a host model, then compare / and %.

Usage: python check-divmod-native.py HOST_GCC DIV_ASM MOD_ASM NEW_OUTPUT_DIR
This checks the instruction-level algorithm, NOT instruction encoding or the ABI;
the four-model board suite covers those independently.
"""
from pathlib import Path
import re,subprocess,sys,json,hashlib
gcc,div,mod,out=sys.argv[1:];out=Path(out);out.mkdir(exist_ok=False)
def translate(path,name):
 text=Path(path).read_text();code=text[text.index('\tcmp dr4,#0'):];code=code[:code.index('\teret')]
 def get(v):
  if v.startswith('#'):return v[1:]+'u'
  if v in ['a','b']:v={'a':'r11','b':'r10'}[v]
  if v.startswith('dr'):return f'd[{int(v[2:])//4}]'
  if v.startswith('wr'):return f'((d[{int(v[2:])//4}] >> {16 if int(v[2:])%4==0 else 0}) & 65535u)'
  if re.fullmatch(r'r\d+',v):return f'((d[{int(v[1:])//4}] >> {8*(3-int(v[1:])%4)}) & 255u)'
  if v in ['dpl','dph']:return v
  raise ValueError(v)
 def put(v,x):
  if v in ['a','b']:v={'a':'r11','b':'r10'}[v]
  if v.startswith('dr'):return f'd[{int(v[2:])//4}] = ({x});'
  if v.startswith('wr'):n=int(v[2:]);shift=16 if n%4==0 else 0;mask=65535<<shift
  elif re.fullmatch(r'r\d+',v):n=int(v[1:]);shift=8*(3-n%4);mask=255<<shift
  elif v in ['dpl','dph']:return f'{v}=({x});'
  else:raise ValueError(v)
  return f'd[{n//4}]=(d[{n//4}] & ~{mask}u) | ((({x}) << {shift}) & {mask}u);'
 lines=[f'static uint32_t {name}(uint32_t x,uint32_t y){{uint32_t d[4]={{x,y,0,0}},dpl=0,dph=0,t,u;bool cy=false,z=false;']
 for line in code.splitlines():
  line=line.strip()
  if not line or line.startswith(';'):continue
  if line.endswith('$:'):lines.append('L'+line[:-2]+':;');continue
  op,arg=line.split(None,1);a=arg.split(',');dest=a[0];src=get(a[1]) if len(a)>1 and op not in ['djnz'] else None
  if op=='mov':s=put(dest,src)
  elif op=='cmp':s=f'cy=({get(dest)} < {src}); z=({get(dest)} == {src});'
  elif op=='add':s=f't={get(dest)};u={src};cy=((uint64_t)t+u>UINT32_MAX);'+put(dest,'t+u')+'z=(t+u==0);'
  elif op=='sub':s=f't={get(dest)};u={src};cy=(t<u);'+put(dest,'t-u')+'z=(t==u);'
  elif op=='inc':s=put(dest,get(dest)+'+1u')
  elif op in ['jc','jnc','je','jne']:s=f'if ({dict(jc="cy",jnc="!cy",je="z",jne="!z")[op]}) goto L{dest[:-1]};'
  elif op in ['sjmp','ejmp']:s=f'goto L{dest[:-1]};'
  elif op=='djnz':s=put(dest,'('+get(dest)+'-1u)&255u')+f'if({get(dest)})goto L{a[1][:-1]};'
  elif op=='div':
   assert dest=='wr2' and a[1]=='wr6'
   s=f't={get(dest)};u={src};assert(u);'+put('wr0','t%u')+put('wr2','t/u')+'cy=false;z=(t/u==0);'
  else:raise ValueError(line)
  lines.append(s)
 lines.append('return (get_unused_placeholder);}')
 lines[-1]=f'return ({get("a")}<<24)|({get("b")}<<16)|(dph<<8)|dpl;}}'
 return '\n'.join(lines)
code='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
'''+translate(div,'qmodel')+'\n'+translate(mod,'rmodel')+'''
static uint32_t seed=0x25140001u;
static uint32_t rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned long tests;
static void check(uint32_t a,uint32_t b){
 uint32_t q=qmodel(a,b),r=rmodel(a,b);
 uint32_t eq=b?a/b:UINT32_MAX,er=b?a%b:a;
 if(q!=eq || r!=er){printf("FAIL %08x %08x q=%08x/%08x r=%08x/%08x\\n",a,b,q,eq,r,er);assert(0);}++tests;
}
int main(void){
 uint32_t e[100]={0,1,2,3,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};unsigned n=8;
 for(unsigned i=1;i<31;++i){e[n++]=(1u<<i)-1;e[n++]=1u<<i;e[n++]=(1u<<i)+1;}
 for(unsigned i=0;i<n;++i)for(unsigned j=0;j<n;++j)check(e[i],e[j]);
 for(unsigned i=0;i<65536;++i){check(i,1);check(i,3);check(i,257);check(i,65535);}
 for(unsigned i=0;i<2000000;++i){uint32_t a=rng(),b=rng();
  switch(i%8){case 0:b&=255;break;case 1:b&=65535;break;case 2:b&=0xffffff;break;
   case 3:a=b;break;case 4:if(b<0x80000000)a=b+b-1;break;case 5:a&=65535;b&=65535;break;}
  check(a,b);
 }
 printf("%lu cases passed\\n",tests);return 0;
}
'''
(out/'model.c').write_text(code)
p=subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra',str(out/'model.c'),'-o',str(out/'model.exe')],capture_output=True);(out/'build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
p=subprocess.run([str(out/'model.exe')],capture_output=True,timeout=90);(out/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stdout+p.stderr
result=dict(kind='instruction-level host model; not target execution',casesPassed=int(p.stdout.split()[0]),assemblerSources=[dict(path=str(Path(x).resolve()),sha256=hashlib.sha256(Path(x).read_bytes()).hexdigest()) for x in [div,mod]])
(out/'results.json').write_text(json.dumps(result,indent=2));print(result)
