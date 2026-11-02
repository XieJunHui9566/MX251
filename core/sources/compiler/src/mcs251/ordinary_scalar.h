/* GPL-2.0-or-later. ABI2 integer parameter facts, independent of target names. */
#ifndef MCS251_ORDINARY_SCALAR_H
#define MCS251_ORDINARY_SCALAR_H
#include "common.h"
static unsigned ordinaryScalarType(sym_link *t)
{
  if(!t || t->next || !IS_SPEC(t) || (!IS_INT(t) && !IS_CHAR(t)) ||
     IS_BITINT(t) || IS_BITFIELD(t) || SPEC_ENUM(t) || isVolatile(t) ||
     isAtomic(t) || isOptional(t) || getAddrspace(t))return 0;
  unsigned n=getSize(t);
  return n==1 || n==2 || n==4?n:0;
}
static bool ordinaryScalarSignature(sym_link *ft,char *out,size_t size)
{
  if(!ft || !IS_FUNC(ft) || FUNC_SDCCCALL(ft)!=2 || !options.stackAuto ||
     options.useXstack || options.noRegParams || FUNC_HASVARARGS(ft) ||
     FUNC_NOPROTOTYPE(ft) || ft->funcAttrs.oldStyle || IFFUNC_ISISR(ft) ||
     IFFUNC_ISBANKEDCALL(ft) || IFFUNC_ISNAKED(ft) || IFFUNC_ISCRITICAL(ft) ||
     IFFUNC_CALLEESAVES(ft) || FUNC_REGBANK(ft) || ft->funcAttrs.rbank ||
     FUNC_ISSHADOWREGS(ft) || FUNC_ISWPARAM(ft) || FUNC_ISOVERLAY(ft) ||
     FUNC_ISBUILTIN(ft) || IFFUNC_ISNORETURN(ft) || IFFUNC_ISSMALLC(ft) ||
     IFFUNC_ISDYNAMICC(ft) || IFFUNC_ISRAISONANCE(ft) || IFFUNC_ISIAR(ft) ||
     IFFUNC_ISCOSMIC(ft) || IFFUNC_ISZ88DK_FASTCALL(ft) ||
     IFFUNC_ISZ88DK_CALLEE(ft) || IFFUNC_ISZ88DK_SHORTCALL(ft))return false;
  for(unsigned i=0;i<sizeof ft->funcAttrs.preserved_regs/sizeof ft->funcAttrs.preserved_regs[0];++i)
    if(ft->funcAttrs.preserved_regs[i])return false;
  unsigned width=ordinaryScalarType(ft->next);
  if(!width && !IS_VOID(ft->next))return false;
  int used=width?snprintf(out,size,"%c%u(",SPEC_USIGN(getSpec(ft->next))?'u':'i',width*8):snprintf(out,size,"v(");
  unsigned count=0;
  for(value *arg=FUNC_ARGS(ft);arg;arg=arg->next) {
    if(!count && !arg->next && IS_VOID(arg->type))break;
    width=ordinaryScalarType(arg->type);
    /* 其它形参可以是对象指针；只描述原运输宽度/地址空间，绝不常量化
     * 指针值或改变访存。未知/函数指针及命名地址空间继续拒绝。 */
    bool pointer=IS_PTR(arg->type) && !IS_FUNCPTR(arg->type) &&
      !getAddrspace(arg->type) && getSize(arg->type)>=1 && getSize(arg->type)<=3;
    if((!width && !pointer) || ++count>8 || used<0 || (size_t)used>=size)return false;
    int n=pointer?snprintf(out+used,size-used,"%sp%u_%u",count>1?",":"",(unsigned)getSize(arg->type),(unsigned)DCL_TYPE(arg->type)):
      snprintf(out+used,size-used,"%s%c%u",count>1?",":"",SPEC_USIGN(getSpec(arg->type))?'u':'i',width*8);
    if(n<0 || (size_t)n>=size-used)return false;used+=n;
  }
  if(used<0 || (size_t)used+2>size)return false;
  strcpy(out+used,")");return true;
}
#endif
