#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../wide_call_reuse.h"
unsigned int mcs251SphAddress=0xd7;

static unsigned state=0x2512026;
static unsigned random_byte(void) {state^=state<<13;state^=state>>17;state^=state<<5;return state&255;}
static unsigned mask(unsigned first,unsigned width) {return ((1u<<width)-1)<<first;}

/* Execute the stack/register transport independently of the optimizer. Calls
 * read their actual arguments, then destroy every caller register. */
static void execute(const char *text,unsigned char *regs,unsigned char *stack,unsigned *sp,unsigned *observed)
{
  for(const char *s=text;*s;) {
    const char *e=strchr(s,'\n');if(!e)e=s+strlen(s);
    char line[160];unsigned n=(unsigned)(e-s);assert(n<sizeof line);memcpy(line,s,n);line[n]=0;
    unsigned r,v,w=1;char op[16],operand[64];
    if(sscanf(line," %15s %63s",op,operand)==2 && line[0]!=';') {
      const char *p=operand;
      if(*p=='w'){w=2;++p;}else if(*p=='d'){w=4;++p;}
      if((!strcmp(op,"push") || !strcmp(op,"pop")) && sscanf(p,"r%u",&r)==1) {
        if(!strcmp(op,"push"))for(unsigned i=0;i<w;++i)stack[(*sp)++]=regs[r+i];
        else for(unsigned i=w;i;--i)regs[r+i-1]=stack[--*sp];
      } else if(sscanf(line," mov r%u,#%u",&r,&v)==2)regs[r]=(unsigned char)v;
      else if(sscanf(line," mov r%u,@spx-%u",&r,&v)==2)regs[r]=stack[*sp-1-v];
      else if(!strcmp(op,"dec") && !strcmp(operand,"spx,#1"))--*sp;
      else if(!strcmp(op,"ecall")) {
        *observed=*observed*33+stack[*sp-1]+regs[6]+(*sp)*7;
        for(unsigned i=0;i<32;++i)regs[i]=(unsigned char)(i+*observed);
      } else assert(0);
    }
    s=*e?e+1:e;
  }
}

static unsigned check(const char *gap,const char *setup,unsigned inputs,unsigned complete,unsigned expect)
{
  char source[4096],optimized[4096];
  snprintf(source,sizeof source,
    "\tpush\tr1\n\tpush\twr4\n\tpush\tdr24\n"
    "; wide restore-begin weight=3\n\tpop\tdr24\n\tpop\twr4\n\tpop\tr1\n; wide restore-end\n%s"
    "; wide save-begin weight=5\n\tpush\tr1\n\tpush\twr4\n\tpush\tdr24\n; wide save-end\n%s"
    "; wide call-inputs touch=%08x complete=%u\n\tecall\t_probe\n",gap,setup,inputs,complete);
  strcpy(optimized,source);wide_call_reuse_result r=wide_reuse_call_saves(optimized,strlen(optimized));
  assert(r.instructions==expect*6 && r.weighted==expect*24 && r.bridges==expect);
  return 1;
}

int main(void)
{
  unsigned checks=0,roundtrips=0;
  checks+=check("","\tmov\tr6,#2\n\tpush\tr6\n",mask(6,1),1,1);
  checks+=check("\tmov\tr6,#2\n","",mask(6,1),1,1);
  checks+=check("\tmov\tr6,r1\n","",mask(6,1),1,0);
  checks+=check("\tmov\tr1,#2\n","",mask(6,1),1,0);
  checks+=check("\tmov\tdr16,spx\n","",0,1,0);
  checks+=check("label:\n","",0,1,0);
  checks+=check("\tsjmp\tlabel\n","",0,1,0);
  checks+=check("\tmov\tr6,@spx\n","",0,1,0);
  checks+=check("; mx251-wordmul-begin\n","",0,1,0);
  checks+=check("; rotate-proposal count=3\n","",0,1,0);
  checks+=check("","",0,0,0);
  for(unsigned r=0;r<32;++r)if((mask(1,1)|mask(4,2)|mask(24,4))&(1u<<r))
    checks+=check("","",1u<<r,1,0);
  /* Three consecutive calls retain the same physical stack bytes. Exercise
   * byte/word/dword saves, stack arguments and SP-relative local reads. */
  for(unsigned trial=0;trial<1000;++trial) {
    char source[8192]="\tpush\tr1\n\tpush\twr4\n\tpush\tdr24\n",optimized[8192];
    for(unsigned call=0;call<3;++call) {
      char segment[1024];snprintf(segment,sizeof segment,
        "; wide save-end\n\tmov\tr6,@spx-7\n\tpush\tr6\n"
        "; wide call-inputs touch=00000040 complete=1\n\tecall\t_probe\n\tdec\tspx,#1\n"
        "; wide restore-begin weight=1\n\tpop\tdr24\n\tpop\twr4\n\tpop\tr1\n; wide restore-end\n%s",
        call<2?"; wide save-begin weight=1\n\tpush\tr1\n\tpush\twr4\n\tpush\tdr24\n":"");
      strcat(source,segment);
    }
    strcpy(optimized,source);wide_call_reuse_result r=wide_reuse_call_saves(optimized,strlen(optimized));
    assert(r.bridges==2 && r.instructions==12);
    unsigned char a[32],b[32],x[128]={0},y[128]={0};unsigned sx=9,sy=9,ox=0,oy=0;
    for(unsigned i=0;i<32;++i)a[i]=b[i]=(unsigned char)random_byte();
    for(unsigned i=0;i<9;++i)x[i]=y[i]=(unsigned char)random_byte();
    execute(source,a,x,&sx,&ox);execute(optimized,b,y,&sy,&oy);
    assert(sx==sy && sx==9 && ox==oy && !memcmp(a,b,32) && !memcmp(x,y,sx));++roundtrips;
  }
  printf("PASS %u input/boundary contracts, %u stack/register executions\n",checks,roundtrips);
}
