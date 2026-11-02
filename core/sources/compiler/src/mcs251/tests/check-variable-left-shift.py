"""Check the actual selector from gen.c against register layouts and bit-level semantics.
Usage: python check-variable-left-shift.py HOST_GCC NEW_OUTPUT_DIR
The host model does not certify target instruction encoding; use the board matrix too.
"""
from pathlib import Path
import sys,subprocess,json
R=Path(__file__).resolve().parent.parent;gen=(R/'gen.c').read_text();gcc,out=sys.argv[1:];out=Path(out);out.mkdir(exist_ok=False)
def extract(name):
 a=gen.index(name+' (');a=gen.rfind('static ',0,a);b=gen.index('{',a);depth=1;i=b+1
 while depth:depth+=(gen[i]=='{')-(gen[i]=='}');i+=1
 return gen[a:i]
source='''#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#define AOP_REG 1
typedef struct {int offset;} reg_info;
typedef struct {int type,size;struct{reg_info *aop_reg[8];}aopu;} asmop;
'''+ '\n'.join(extract(n) for n in ['mcs251WordForNativePair','mcs251DwordForNativeTuple','mcs251NativeLeftShiftRegister'])+'''
int main(void){reg_info regs[18];asmop a={1,0};uint32_t layouts=0,accepted=0,semantics=0,seed=0x25150001u;
 for(int i=0;i<18;++i)regs[i].offset=i;
 for(int size=2;size<=4;size+=2){a.size=size;uint32_t total=1u<<(size*4);
  for(uint32_t x=0;x<total;++x){uint32_t bits=x;
   for(int i=0;i<size;++i){a.aopu.aop_reg[i]=regs+(bits&15);bits>>=4;}
   int hi=a.aopu.aop_reg[size-1]->offset;bool valid=(hi%size==0);
   for(int i=0;i<size;++i)valid &= a.aopu.aop_reg[i]->offset==hi+size-1-i && a.aopu.aop_reg[i]->offset!=10 && a.aopu.aop_reg[i]->offset!=11;
   const char *name=mcs251NativeLeftShiftRegister(&a);assert((name!=NULL)==valid);++layouts;if(!name)continue;++accepted;
   char want[16];sprintf(want,"%s%d",size==2?"wr":"dr",hi);assert(!strcmp(want,name));
   for(int p=0;p<1024;++p){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;uint32_t v=seed,mask=size==2?65535u:UINT32_MAX;v&=mask;
    for(int n=0;n<8*size;++n){uint8_t before[16],actual[16];for(int j=0;j<16;++j)before[j]=(uint8_t)(0xa5+j);
     for(int j=0;j<size;++j)before[hi+j]=(uint8_t)(v>>(8*(size-1-j)));memcpy(actual,before,16);
     uint32_t q=v;for(int j=0;j<n;++j)q=(q+q)&mask;
     for(int j=0;j<size;++j)actual[hi+j]=(uint8_t)(q>>(8*(size-1-j)));
     assert(q==((v<<n)&mask));for(int j=0;j<16;++j)if(j<hi||j>=hi+size)assert(actual[j]==before[j]);++semantics;
    }
   }
  }
 }
 a.type=0;assert(!mcs251NativeLeftShiftRegister(&a));a.type=1;
 for(int s=0;s<8;++s)if(s!=2&&s!=4){a.size=s;assert(!mcs251NativeLeftShiftRegister(&a));}
 a.size=2;a.aopu.aop_reg[0]=regs+17;a.aopu.aop_reg[1]=regs+16;assert(!mcs251NativeLeftShiftRegister(&a));
 printf("%u %u %u\\n",layouts,accepted,semantics);return 0;}
'''
(out/'selector.c').write_text(source)
p=subprocess.run([gcc,'-O2','-std=c11','-Wall','-Wextra',str(out/'selector.c'),'-o',str(out/'selector.exe')],capture_output=True);(out/'build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
p=subprocess.run([str(out/'selector.exe')],capture_output=True,timeout=60);(out/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stdout+p.stderr
n,a,s=map(int,p.stdout.split());result=dict(registerLayouts=n,acceptedLayouts=a,semanticStates=s,model='actual compiler selector plus host bit/register model; not MCU execution');(out/'results.json').write_text(json.dumps(result,indent=2));print(result)
