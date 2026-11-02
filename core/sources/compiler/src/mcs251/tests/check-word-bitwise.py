"""运行实际生成器 helper 的宿主测试；不冒充目标 CPU 指令模拟。"""
from pathlib import Path
import subprocess,json,sys
if len(sys.argv)!=3:
    raise SystemExit('usage: check-word-bitwise.py HOST_CC OUTPUT_DIRECTORY')
D=Path(sys.argv[2]).resolve();D.mkdir(parents=True,exist_ok=False)
text=(Path(__file__).resolve().parent.parent/'gen.c').read_text(encoding='utf-8')
def function(name):
    start=text.index(name+' (');start=text.rfind('static ',0,start)
    end=text.index('{',start);depth=1;i=end+1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[start:i]
prefix=r'''
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define TRUE true
#define FALSE false
#define AOP_REG 1
typedef struct { int offset; } reg_info;
typedef struct { int type,size; struct { reg_info *aop_reg[8]; } aopu; } asmop;
typedef struct { asmop *aop; int bitint; } operand;
#define AOP(o) ((o)->aop)
#define AOP_TYPE(o) ((o)->aop->type)
#define AOP_SIZE(o) ((o)->aop->size)
#define operandType(o) ((o)->bitint)
#define IS_BITINT(t) (t)
static int count,dest[2],src[2];static char operation;
static bool sameRegs(const asmop *a,const asmop *b) {
 if(a->type!=b->type||a->size!=b->size)return false;
 for(int i=0;i<a->size;i++)if(a->aopu.aop_reg[i]!=b->aopu.aop_reg[i])return false;
 return true;
}
static void emitcode(const char *op,const char *fmt,...) {
 va_list ap;va_start(ap,fmt);const char *d=va_arg(ap,const char*),*s=va_arg(ap,const char*);va_end(ap);
 if(count==2)abort();dest[count]=atoi(d+2);src[count]=atoi(s+2);operation=op[0];count++;
}
'''
suffix=r'''
static reg_info regs[18];
static asmop layouts[42];
static int make_layouts(int size) {
 int n=0;int pairs[]={0,2,4,6,8,12,14};
 for(int i=0;i<7;i++)for(int j=0;j<(size==4?7:1);j++) {
  if(size==4&&i==j)continue;
  asmop *p=&layouts[n++];p->type=AOP_REG;p->size=size;
  p->aopu.aop_reg[0]=regs+pairs[i]+1;p->aopu.aop_reg[1]=regs+pairs[i];
  if(size==4){p->aopu.aop_reg[2]=regs+pairs[j]+1;p->aopu.aop_reg[3]=regs+pairs[j];}
 }
 return n;
}
static unsigned char logical(char op,unsigned char a,unsigned char b) {
 return op=='a'?(a&b):op=='o'?(a|b):(a^b);
}
int main(void) {
 uint32_t tests=0,accepted=0,rejected=0;char *ops[]={(char*)"anl",(char*)"orl",(char*)"xrl"};
 for(int i=0;i<18;i++)regs[i].offset=i;
 for(int size=2;size<=4;size+=2) {
  int n=make_layouts(size);
  for(int l=0;l<n;l++)for(int r=0;r<n;r++)for(int d=0;d<n;d++)for(int op=0;op<3;op++) {
   operand a={layouts+l,0},b={layouts+r,0},z={layouts+d,0};count=0;
   bool ok=mcs251GenWordBitwise(&a,&b,&z,ops[op]);
   if(!ok){if(count)abort();rejected++;continue;}
   accepted++;if(count!=size/2)abort();
   for(int pattern=0;pattern<16;pattern++) {
    unsigned char before[18],actual[18],expected[18];
    uint32_t seed=0x80510000u+pattern;
    for(int i=0;i<18;i++){seed=1664525u*seed+1013904223u;before[i]=seed>>24;}
    if(pattern==0)memset(before,0,sizeof before);
    if(pattern==1)memset(before,255,sizeof before);
    memcpy(actual,before,sizeof actual);memcpy(expected,before,sizeof expected);
    for(int i=0;i<size;i++)expected[z.aop->aopu.aop_reg[i]->offset]=logical(ops[op][0],before[a.aop->aopu.aop_reg[i]->offset],before[b.aop->aopu.aop_reg[i]->offset]);
    for(int i=0;i<count;i++) {
     uint16_t x=(actual[dest[i]]<<8)|actual[dest[i]+1],y=(actual[src[i]]<<8)|actual[src[i]+1];
     uint16_t v=operation=='a'?(x&y):operation=='o'?(x|y):(x^y);
     actual[dest[i]]=v>>8;actual[dest[i]+1]=v;
    }
    if(memcmp(actual,expected,sizeof actual)){fprintf(stderr,"alias regression %d %d %d %d\n",size,l,r,d);return 1;}
    tests++;
   }
  }
 }
 make_layouts(4);
 for(int variant=0;variant<6;variant++) {
  asmop aa=layouts[0],bb=layouts[1],zz=layouts[0];operand a={&aa,0},b={&bb,0},z={&zz,0};
  if(variant==0)a.bitint=1;
  if(variant==1)bb.type=2;
  if(variant==2)zz.size=3;
  if(variant==3)aa.size=2;
  if(variant==4){aa.aopu.aop_reg[0]=regs+16;zz=aa;}
  if(variant==5){aa.aopu.aop_reg[0]=regs+2;zz=aa;}
  count=0;if(mcs251GenWordBitwise(&a,&b,&z,"xrl")||count)abort();
 }
 printf("{\"registerStateChecks\":%u,\"acceptedLayouts\":%u,\"fallbackLayouts\":%u,\"negativeGuards\":6}\n",tests,accepted,rejected);
 return 0;
}
'''
(D/'test.c').write_text(prefix+'\n'.join(function(n) for n in ['mcs251WordForNativePair','mcs251CrossByteRegOverlap','mcs251GenWordBitwise'])+suffix,encoding='utf-8')
cmd=[sys.argv[1],'-std=c11','-O2',str(D/'test.c'),'-o',str(D/'test.exe')]
p=subprocess.run(cmd,capture_output=True);(D/'build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0
p=subprocess.run([str(D/'test.exe')],capture_output=True,timeout=30);(D/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
result=json.loads(p.stdout);result['targetCpuSimulated']=False
(D/'results.json').write_text(json.dumps(result,indent=2));print(result)
