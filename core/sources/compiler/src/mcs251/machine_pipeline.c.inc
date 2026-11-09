/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_ir.h"
extern int mcs251MachineIrEnabled;
extern bool mcs251CallClobberWrites(symbol *,unsigned long *);
static int mcs251MachineCall(const char *name,uint32_t *reads,uint32_t *writes,void *context) {
  ebbIndex *ebbi=context;symbol *callee=NULL;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next)if(ic->op==CALL && IS_SYMOP(IC_LEFT(ic))) {
      symbol *s=OP_SYMBOL(IC_LEFT(ic));
      if(!strcmp(name,s->rname)) {callee=s;break;}
    }
  }
  if(!callee || !IS_FUNC(callee->type) || IFFUNC_ISNAKED(callee->type) ||
     IFFUNC_HASVARARGS(callee->type) || FUNC_NOPROTOTYPE(callee->type) ||
     IFFUNC_ISBANKEDCALL(callee->type) || FUNC_REGBANK(callee->type))return 0;
  unsigned abi=FUNC_SDCCCALL(callee->type);
  if(abi!=2 && abi!=4)return 0;
  unsigned long mask;
  *writes=mcs251CallClobberWrites(callee,&mask)?(uint32_t)mask:UINT32_MAX;
  *reads=0;
  if(mcs251InternalArgument(callee)) {*reads=mcs251RegisterBytes(4,4);return 1;}
  unsigned index=0;
  for(value *arg=FUNC_ARGS(callee->type);arg;arg=arg->next,++index) {
    unsigned width=getSize(arg->type);
    if(IS_BIT(arg->type) || width>4)return 0;
    if(abi==2 && !index) {
      if(width>=3)*reads|=UINT32_C(1)<<10;
      if(width==4)*reads|=UINT32_C(1)<<11;
    } else if(abi==4 && index<2)*reads|=mcs251RegisterBytes(index?12:4,4);
  }
  return 1;
}
static int mcs251MachineMemory(const char *name,void *unused) {
  (void)unused;
  memmap *spaces[]={data,sfr,sfrbit,bit};
  for(unsigned i=0;i<sizeof spaces/sizeof spaces[0];++i)if(spaces[i])
    for(const set *node=spaces[i]->syms;node;node=node->next) {
      const symbol *s=node->item;
      if(!s || !s->rname[0] || getSize(s->type)<=0)continue;
      int offset=wide_cost_symbol_offset(name,s->rname,(unsigned)getSize(s->type));
      if(offset<0)continue;
      if(SPEC_ABSA(s->etype)) {
        unsigned address=SPEC_ADDR(s->etype)+offset;
        if(address<32 || (spaces[i]==sfr && (address==0xd0 || address==0xd1 || address==0xe0 || address==0xf0)))return -1;
        if(spaces[i]==sfrbit && ((address&0xf8)==0xd0 || (address&0xf8)==0xe0 || (address&0xf8)==0xf0))return -1;
      }
      return 1;
    }
  return 0;
}
static void mcs251MachinePipeline(size_t start,ebbIndex *ebbi) {
  if(!mcs251MachineIrEnabled || options.debug || options.nopeep || !currFunc ||
     IFFUNC_ISNAKED(currFunc->type) || IFFUNC_ISISR(currFunc->type) ||
     IFFUNC_CALLEESAVES(currFunc->type) || FUNC_REGBANK(currFunc->type) ||
     mcs251GetEmittedRegisterBank())return;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next)
      if(ic->op==INLINEASM || ic->op==JUMPTABLE)return;
  }
  const char *body=dbuf_get_buf(codeOutBuf)+start;
  size_t length=dbuf_get_length(codeOutBuf)-start;
  /* Keep the complete return GPR window. The public ABI leaves other GPRs
   * caller-clobbered; preserved-register entries were excluded above. */
  uint32_t exits=FUNC_SDCCCALL(currFunc->type)==2 && !mcs251InternalReturn(currFunc)?
    MCS251_AB_BYTES:mcs251RegisterBytes(4,4);
  /* POPs at the function boundary are observable by callers using the
   * verified preservation summary, independently of the return value. */
  if(strstr(body,"; mx251-callee-preserved schema=1 "))exits|=UINT32_C(0xff00f300);
  unsigned counts[3];char *optimized=mi_optimize(body,length,exits,mcs251MachineMemory,mcs251MachineCall,ebbi,counts);
  if(optimized) {
    dbuf_set_length(codeOutBuf,start);dbuf_append_str(codeOutBuf,optimized);free(optimized);
  }
}
