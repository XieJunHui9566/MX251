/* SPDX-License-Identifier: GPL-2.0-or-later
 * 同一IR的完整普通/宽后端候选。只比较无分支的普通ABI函数，所有CALL
 * 次序必须一致；未知编码、额外辅助体或不平衡栈保留原宽后端候选。
 * 不交换ASM片段，不改变声明/调用约定，也不以函数或器件名称选择。 */
#include "internal_return.h"
#include "wide_cost.h"
#include "backend_encoding.h"
#include "instruction_effects.h"

typedef struct {
  unsigned bytes,instructions,stackRead,stackWrite,peak,unknown,calls;
  uint32_t registerWrites;
  char callNames[64][256];
} mcs251_backend_cost;

static bool
mcs251BackendChoiceEligible(ebbIndex *ebbi)
{
  const char *enabled=getenv("MX251_LINEAR_BACKEND_CHOICE");
  if(enabled && !strcmp(enabled,"0"))return false;
  if(!currFunc || FUNC_SDCCCALL(currFunc->type)!=2 ||
     mcs251InternalReturn(currFunc) || mcs251InternalArgument(currFunc) ||
     IFFUNC_ISISR(currFunc->type) || options.debug)return false;
  unsigned entries=0,ends=0,returns=0;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      if(ic->op==FUNCTION)++entries;
      if(ic->op==ENDFUNCTION)++ends;
      if(ic->op==RETURN)++returns;
      if(ic->op==JUMPTABLE || ic->op==PCALL || ic->op==INLINEASM)return false;
      if(ic->op==CALL) {
        operand *op=IC_LEFT(ic);
        if(!op || !IS_SYMOP(op) || !IS_FUNC(operandType(op)) ||
           FUNC_SDCCCALL(operandType(op))!=2 || OP_SYMBOL(op)->cdef ||
           mcs251InternalReturn(OP_SYMBOL(op)) || mcs251InternalArgument(OP_SYMBOL(op)))return false;
      }
    }
  }
  return entries==1 && ends==1;
}

static wide_cost_operand
mcs251BackendSymbol(const char *name,const void *unused)
{
  (void)unused;
  wide_cost_operand result={WC_UNKNOWN,0,0};
  if(!strcmp(name,"dpxl")){result.kind=WC_DIRECT;return result;}
  if(strlen(name)==3 && name[0]=='a' && name[1]=='r' && name[2]>='0' && name[2]<='7') {
    result.kind=WC_DIRECT;return result;
  }
  memmap *spaces[]={data,sfr,sfrbit,bit};
  for(unsigned i=0;i<sizeof spaces/sizeof spaces[0];++i)if(spaces[i])
    for(const set *node=spaces[i]->syms;node;node=node->next) {
      const symbol *s=node->item;
      if(!s || !s->rname[0] || getSize(s->type)<=0)continue;
      if(wide_cost_symbol_offset(name,s->rname,(unsigned)getSize(s->type))<0)continue;
      result.kind=IN_BITSPACE(spaces[i])?WC_BIT:WC_DIRECT;return result;
    }
  return result;
}

/* 这是显式 GPR 目的操作数的成本约束，不是内存别名或调用保存证明。
 * 最终调用破坏闭包仍由正常构建驱动对实际 ASM/REL 独立核对。 */
static uint32_t
mcs251BackendExplicitRegister(const char *text)
{
  mx251_effect_operand operand;
  if(mx251EffectRegister(text,&operand))return operand.gpr;
  if(strlen(text)==3 && text[0]=='a' && text[1]=='r' && text[2]>='0' && text[2]<='7')
    return UINT32_C(1)<<(text[2]-'0');
  return 0;
}

static bool
mcs251BackendNumber(const char *text,unsigned *out)
{
  if(*text++!='#' || !isdigit((unsigned char)*text))return false;
  char *end;unsigned long value=strtoul(text,&end,0);
  if(*end || end==text || value>65535)return false;
  *out=(unsigned)value;return true;
}

static mcs251_backend_cost
mcs251BackendCost(const char *text,size_t length)
{
  mcs251_backend_cost c={0};int depth=0;unsigned returns=0;bool ended=false;
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    const char *p=text+at,*last=text+end;at=end+1;
    while(p<last && isspace((unsigned char)*p))++p;
    const char *comment=memchr(p,';',(size_t)(last-p));if(comment)last=comment;
    while(last>p && isspace((unsigned char)last[-1]))--last;
    if(last==p)continue;
    if(last[-1]==':')continue;
    /* 旧后端寄存器bank0别名没有机器字节；只接受完整编译器形式。 */
    unsigned ar=0,value=0;int consumed=0;char temp[512];size_t n=(size_t)(last-p);
    if(n>=sizeof temp){++c.unknown;continue;}memcpy(temp,p,n);temp[n]=0;
    if(sscanf(temp,"ar%u = 0x%x%n",&ar,&value,&consumed)==2 && consumed==(int)n && ar<8 && value==ar)continue;
    unsigned bytes=bc_instruction_bytes(p,n,mcs251BackendSymbol,NULL);
    if(!bytes || ended){++c.unknown;continue;}
    char op[24],args[512];size_t k=0,j=0;
    while(k<n && !isspace((unsigned char)temp[k]) && k+1<sizeof op){op[k]=(char)tolower((unsigned char)temp[k]);++k;}
    op[k]=0;
    for(;k<n;++k)if(!isspace((unsigned char)temp[k]))args[j++]=temp[k];args[j]=0;
    char *second=strchr(args,',');if(second)*second++=0;
    if((op[0]=='j') || !strcmp(op,"sjmp") || !strcmp(op,"ljmp") || !strcmp(op,"ejmp") || !strcmp(op,"djnz") || !strcmp(op,"cjne")){++c.unknown;continue;}
    c.bytes+=bytes;++c.instructions;
    wide_cost_operand a=wide_cost_arg(args),b=second?wide_cost_arg(second):(wide_cost_operand){0};
    if(a.kind==WC_UNKNOWN)a=mcs251BackendSymbol(args,NULL);
    if(second && b.kind==WC_UNKNOWN)b=mcs251BackendSymbol(second,NULL);
    if(strcmp(op,"push") && strcmp(op,"pushw") && strcmp(op,"cmp") &&
       strcmp(op,"ecall") && strcmp(op,"eret") && strcmp(op,"nop")) {
      c.registerWrites|=mcs251BackendExplicitRegister(args);
      if(second && (!strcmp(op,"xch") || !strcmp(op,"mul") || !strcmp(op,"div")))
        c.registerWrites|=mcs251BackendExplicitRegister(second);
    }
    if(!strcmp(op,"push") || !strcmp(op,"pop") || !strcmp(op,"pushw")) {
      unsigned width=!strcmp(op,"pushw")?2:a.kind==WC_REG?(unsigned)a.width:1;
      if(!strcmp(op,"pop")){depth-=(int)width;c.stackRead+=width;}
      else {depth+=(int)width;c.stackWrite+=width;}
    } else if((!strcmp(args,"spx") || !strcmp(args,"dr60")) &&
              (!strcmp(op,"add") || !strcmp(op,"sub") || !strcmp(op,"inc") || !strcmp(op,"dec"))) {
      unsigned amount=1;if(second && !mcs251BackendNumber(second,&amount)){++c.unknown;continue;}
      depth+=(!strcmp(op,"add") || !strcmp(op,"inc"))?(int)amount:-(int)amount;
    } else if(!strcmp(op,"ecall")) {
      if(second || c.calls==64 || strlen(args)>=sizeof c.callNames[0]){++c.unknown;continue;}
      strcpy(c.callNames[c.calls++],args);
      if(depth>=0 && (unsigned)(depth+3)>c.peak)c.peak=(unsigned)depth+3;
    } else if(!strcmp(op,"eret")) {++returns;ended=true;if(depth)++c.unknown;}
    else if(!strcmp(op,"mov")) {
      if(strstr(args,"@spx") || strstr(args,"@dr60"))c.stackWrite+=b.kind==WC_REG?(unsigned)b.width:1;
      if(second && (strstr(second,"@spx") || strstr(second,"@dr60")))c.stackRead+=a.kind==WC_REG?(unsigned)a.width:1;
      if(!strcmp(args,"spx") || !strcmp(args,"dr60"))++c.unknown;
    }
    if(depth<0 || depth>65535)++c.unknown;
    else if((unsigned)depth>c.peak)c.peak=(unsigned)depth;
  }
  if(returns!=1 || depth)++c.unknown;
  return c;
}

static bool
mcs251BackendChooseLegacy(const mcs251_backend_cost *wide,const mcs251_backend_cost *legacy)
{
  if(wide->unknown || legacy->unknown || wide->calls!=legacy->calls ||
     (legacy->registerWrites & ~wide->registerWrites))return false;
  for(unsigned i=0;i<wide->calls;++i)if(strcmp(wide->callNames[i],legacy->callNames[i]))return false;
  return legacy->bytes<wide->bytes && legacy->instructions<=wide->instructions &&
    legacy->stackRead<=wide->stackRead && legacy->stackWrite<=wide->stackWrite && legacy->peak<=wide->peak;
}

#include "backend_cfg.h"
