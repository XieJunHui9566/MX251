#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../call_save_plan.h"
#include "../call_merge.h"

static unsigned state=0x25125001u;
static unsigned next(void) { state^=state<<13;state^=state>>17;state^=state<<5;return state; }
static unsigned byte_first(unsigned mask) { unsigned n=0;while (!(mask&(1u<<n)))++n;return n; }
static void push(unsigned char *stack,unsigned *sp,unsigned char *regs,unsigned mask,unsigned width)
{ unsigned first=byte_first(mask);for(unsigned j=0;j<width;++j)stack[(*sp)++]=regs[first+j]; }
static void pop(unsigned char *stack,unsigned *sp,unsigned char *regs,unsigned mask,unsigned width)
{ unsigned first=byte_first(mask);for(unsigned j=width;j;--j)regs[first+j-1]=stack[--*sp]; }
int main(void)
{
  unsigned plans=0,rejects=0,roundtrips=0;
  for(unsigned mask=1;mask<65536;++mask)
    {
      mcs251_call_save_plan plan; if(!mcs251BuildCallSavePlan(mask,&plan))continue;
      mcs251_call_merge_op ops[40];unsigned n=0,remove[32];
      for(unsigned i=plan.count;i;--i)
        { mcs251_call_save_step s=plan.steps[i-1]; ops[n++]=(mcs251_call_merge_op){M251_CM_POP,((1u<<s.width)-1)<<s.first,s.width}; }
      ops[n++]=(mcs251_call_merge_op){M251_CM_MOVE,3u<<16,2};
      for(unsigned i=0;i<plan.count;++i)
        { mcs251_call_save_step s=plan.steps[i];ops[n++]=(mcs251_call_merge_op){M251_CM_PUSH,((1u<<s.width)-1)<<s.first,s.width}; }
      ops[n++]=(mcs251_call_merge_op){M251_CM_CALL,3u<<16,0};
      assert(mcs251CallMergeFind(ops,n,remove)==plan.count*2);++plans;
      for(unsigned r=0;r<16;++r)if(mask&(1u<<r))
        {
          ops[plan.count].mask|=1u<<r;assert(!mcs251CallMergeFind(ops,n,remove));ops[plan.count].mask=3u<<16;++rejects;
          ops[n-1].mask|=1u<<r;assert(!mcs251CallMergeFind(ops,n,remove));ops[n-1].mask=3u<<16;++rejects;
        }
      for(unsigned r=1;r<=plan.count;++r)
        { unsigned saved=ops[plan.count+r].width;ops[plan.count+r].width=saved+1;
          assert(!mcs251CallMergeFind(ops,n,remove));ops[plan.count+r].width=saved;++rejects; }
      assert(!mcs251CallMergeFind(ops,n-1,remove));++rejects;
      ops[plan.count].kind=M251_CM_STOP;assert(!mcs251CallMergeFind(ops,n,remove));ops[plan.count].kind=M251_CM_MOVE;++rejects;
      for(unsigned trial=0;trial<4;++trial)
        {
          unsigned char old[16],now[16],stack1[64],stack2[64],clobber[16];unsigned sp1=7,sp2=7;
          for(unsigned i=0;i<16;++i)old[i]=now[i]=(unsigned char)next();
          for(unsigned i=0;i<7;++i)stack1[i]=stack2[i]=(unsigned char)next();
          for(unsigned i=0;i<plan.count;++i)
            { mcs251_call_save_step s=plan.steps[i];unsigned m=((1u<<s.width)-1)<<s.first;
              push(stack1,&sp1,old,m,s.width);push(stack2,&sp2,now,m,s.width); }
          for(unsigned i=0;i<16;++i)old[i]=now[i]=(unsigned char)next();
          for(unsigned i=0;i<n;++i)
            { if(ops[i].kind==M251_CM_POP)pop(stack1,&sp1,old,ops[i].mask,ops[i].width);
              if(ops[i].kind==M251_CM_PUSH)push(stack1,&sp1,old,ops[i].mask,ops[i].width); }
          assert(sp1==sp2 && !memcmp(stack1,stack2,sp1));
          for(unsigned i=0;i<16;++i)clobber[i]=(unsigned char)next();
          memcpy(old,clobber,16);memcpy(now,clobber,16);
          for(unsigned i=plan.count;i;--i)
            { mcs251_call_save_step s=plan.steps[i-1];unsigned m=((1u<<s.width)-1)<<s.first;
              pop(stack1,&sp1,old,m,s.width);pop(stack2,&sp2,now,m,s.width); }
          assert(sp1==7 && sp2==7 && !memcmp(old,now,16) && !memcmp(stack1,stack2,7));++roundtrips;
        }
    }
  const char *barriers[]={"mov a,@r0","mov a,_shared","mov _shared,a","mov r1,sp","mov sp,r1","mov psw,#0","mov dptr,#(_x)","mov dpx,#1","mov wr3,wr4","movx a,@dptr","anl a,#1","pop dr8","push bits","mov r2,#1 garbage","call _unknown"};
  for(unsigned i=0;i<sizeof barriers/sizeof *barriers;++i){assert(!mcs251CallMergeParse(barriers[i]).kind);++rejects;}
  assert(mcs251CallMergeParse("mov r8,wr8").kind==0);
  assert(mcs251CallMergeParse("mov wr8,wr12").mask==0x3300);
  assert(mcs251CallMergeParse("mov r13,#0xff").mask==(1u<<13));
  assert(mcs251CallMergeParse("push dr12").mask==0xf000);
  printf("PASS %u plans, %u rejection cases, %u stack/register roundtrips\n",plans,rejects,roundtrips);
}
