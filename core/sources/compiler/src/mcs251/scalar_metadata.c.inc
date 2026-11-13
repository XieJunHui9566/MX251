#include "ordinary_scalar.h"
static void ordinaryScalarMetadata(ebbIndex *ebbi)
{
  char signature[128];
  if(ordinaryScalarSignature(currFunc->type,signature,sizeof signature))
    dbuf_printf(codeOutBuf,"; mx251-scalar-definition schema=1 symbol=%s signature=%s\n",currFunc->rname,signature);
  else if(FUNC_SDCCCALL(currFunc->type)==4 && ordinaryScalarSignatureMode(currFunc->type,signature,sizeof signature,true))
    dbuf_printf(codeOutBuf,"; mx251-near-definition schema=1 symbol=%s signature=%s\n",currFunc->rname,signature);
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      if(ic->op!=CALL || !IC_LEFT(ic) || IC_LEFT(ic)->type!=SYMBOL)continue;
      symbol *callee=OP_SYMBOL(IC_LEFT(ic));sym_link *ft=operandType(IC_LEFT(ic));
      if(IS_FUNC(ft) && FUNC_SDCCCALL(ft)==4 && ordinaryScalarSignatureMode(ft,signature,sizeof signature,true))
        dbuf_printf(codeOutBuf,"; mx251-near-call schema=1 owner=%s target=%s signature=%s\n",currFunc->rname,callee->rname,signature);
      if(!ordinaryScalarSignature(ft,signature,sizeof signature))continue;
      dbuf_printf(codeOutBuf,"; mx251-scalar-call schema=1 owner=%s target=%s signature=%s values=",currFunc->rname,callee->rname,signature);
      unsigned ordinal=0;
      for(value *formal=FUNC_ARGS(ft);formal;formal=formal->next) {
        if(!ordinal && !formal->next && IS_VOID(formal->type))break;
        if(ordinal++)dbuf_printf(codeOutBuf,",");
        iCode *setup=NULL;unsigned walked=0;bool duplicate=false;
        for(iCode *p=ic->prev;p && ++walked<=1024;p=p->prev) {
          if(p->eBBlockNum!=ic->eBBlockNum || p->op==CALL || p->op==PCALL || p->op==INLINEASM ||
             p->op==LABEL || p->op==GOTO || p->op==IFX || p->op==FUNCTION || p->op==RETURN || p->op==JUMPTABLE)break;
          if((p->op==SEND || (p->op==IPUSH && p->parmPush)) && IC_RIGHT(p) &&
             IC_RIGHT(p)->type==VALUE && OP_VALUE(IC_RIGHT(p))==formal) {
            if(setup){duplicate=true;break;}setup=p;
          }
        }
        operand *actual=setup?IC_LEFT(setup):NULL;unsigned width=ordinaryScalarType(formal->type);
        if(!width || duplicate || walked>1024 || !actual || !IS_OP_LITERAL(actual) ||
           !ordinaryScalarType(operandType(actual))) {
          dbuf_printf(codeOutBuf,"?");continue;
        }
        /* SEND/IPUSH literals may retain a narrower type or different sign.
         * Record the formal's transported bits, including integer conversion,
         * rather than requiring identical literal and parameter types. */
        unsigned long long mask=(1ULL<<(width*8))-1;
        dbuf_printf(codeOutBuf,"%08lx",(unsigned long)(operandLitValueUll(actual)&mask));
      }
      if(!ordinal)dbuf_printf(codeOutBuf,"-");dbuf_printf(codeOutBuf,"\n");
    }
  }
}
