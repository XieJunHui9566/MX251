/* SPDX-License-Identifier: GPL-2.0-or-later
 * 同一 IR 的完整普通/宽后端候选。现有选择比较线性体或共同 CFG 区域；
 * 无环 C 控制流及窄无符号取负使用完整体积与静态指令数选择。
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
mcs251BackendChoiceEligible(ebbIndex *ebbi, bool narrowTrial)
{
  const char *enabled=getenv("MX251_LINEAR_BACKEND_CHOICE");
  if(enabled && !strcmp(enabled,"0"))return false;
  if(!currFunc || FUNC_SDCCCALL(currFunc->type)!=2 ||
     mcs251InternalReturn(currFunc) || mcs251InternalArgument(currFunc) ||
     IFFUNC_ISISR(currFunc->type) || options.debug)return false;
  bool ordered=false;
  if(mcs251OrderedAsmEnabled)for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next)
      if(ic->op==INLINEASM)ordered=true;
  }
  unsigned entries=0,ends=0,returns=0;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      if(ic->op==FUNCTION)++entries;
      if(ic->op==ENDFUNCTION)++ends;
      if(ic->op==RETURN)++returns;
      /* Ordered functions use complete-body costs, including jump-table
       * dispatch and entries; they do not use the common-CFG selector. */
      if((ic->op==JUMPTABLE && !ordered) || ic->op==PCALL)return false;
      /* Comparison only runs after successful wide import, which validates
       * every inline instruction and its target bit contract. */
      if(ic->op==INLINEASM && !mcs251OrderedAsmEnabled)return false;
      if(ic->op==CALL) {
        operand *op=IC_LEFT(ic);
        if(!op || !IS_SYMOP(op) || !IS_FUNC(operandType(op)) ||
           (FUNC_SDCCCALL(operandType(op))!=2 &&
            !(ordered && FUNC_SDCCCALL(operandType(op))==4)) ||
           (!narrowTrial && OP_SYMBOL(op)->cdef) ||
           mcs251InternalReturn(OP_SYMBOL(op)) || mcs251InternalArgument(OP_SYMBOL(op)))return false;
      }
    }
  }
  return entries==1 && ends==1;
}

/* Narrow unsigned negation can admit an entire function, including its loops.
 * Compare both complete bodies before choosing it. The ordinary wide importer
 * validates operations and CFG; selection requires fewer bytes and no more
 * static instructions. This does not estimate loop execution counts. */
static bool
mcs251BackendNarrowTrial(ebbIndex *ebbi)
{
  const char *mode=getenv("MX251_NARROW_UNSIGNED_NEGATE");
  if(mode || !mcs251BackendChoiceEligible(ebbi,true))return false;
  bool found=false;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      if(ic->op==UNARYMINUS) {
        sym_link *t=operandType(IC_RESULT(ic));int width=getSize(t);
        if(IS_INTEGRAL(t) && SPEC_USIGN(getSpec(t)) && (width==1 || width==2))found=true;
      }
    }
  }
  return found;
}

/* Whole-body selection for acyclic C control flow. Unlike the path-dominance
 * selector, this compares both complete implementations, including branch
 * encodings and shared tails. It is a size/instruction-count heuristic, not
 * a claim that every input takes the same number of cycles. */
static bool
mcs251BackendBodyEligible(ebbIndex *ebbi)
{
  const char *enabled=getenv("MX251_BODY_BACKEND_CHOICE");
  if(enabled && !strcmp(enabled,"0"))return false;
  /* Newly admitted ordered functions often contain byte-oriented I/O loops.
   * Compare complete bodies as well: retain the byte backend when it has
   * fewer bytes and no more static instructions. This is a profitability
   * heuristic, not a bound on execution time for every loop trip count. */
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next)
      if(ic->op==INLINEASM)return true;
  }
  unsigned *degree=Safe_calloc(ebbi->count,sizeof *degree);
  unsigned remaining=0;bool valid=true;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    if(bb->noPath){degree[b]=UINT_MAX;continue;}
    ++remaining;
    for(const set *s=bb->succList;s;s=s->next) {
      eBBlock *next=s->item;if(next->noPath)continue;
      int j=0;for(;j<ebbi->count && ebbi->bbOrder[j]!=next;++j){}
      if(j==ebbi->count){valid=false;break;}
      ++degree[j];
    }
  }
  while(valid && remaining) {
    int b=0;for(;b<ebbi->count && degree[b];++b){}
    if(b==ebbi->count){valid=false;break;}
    degree[b]=UINT_MAX;--remaining;
    for(const set *s=ebbi->bbOrder[b]->succList;s;s=s->next) {
      eBBlock *next=s->item;if(next->noPath)continue;
      for(int j=0;j<ebbi->count;++j)if(ebbi->bbOrder[j]==next){--degree[j];break;}
    }
  }
  Safe_free(degree);return valid;
}

static wide_cost_operand
mcs251BackendSymbol(const char *name,const void *unused)
{
  (void)unused;
  wide_cost_operand result={WC_UNKNOWN,0,0};
  if(name[0]=='b' && name[1]>='0' && name[1]<='7' && !name[2])
    return (wide_cost_operand){WC_BIT,0,0};
  if(strlen(name)==6 && name[0]=='0' && (name[1]=='x' || name[1]=='X') &&
     isxdigit((unsigned char)name[2]) && isxdigit((unsigned char)name[3]) &&
     name[4]=='.' && name[5]>='0' && name[5]<='7') {
    unsigned address=(unsigned)strtoul(name,NULL,16);
    if(address>=0x80)return (wide_cost_operand){address&7?WC_EXT_BIT:WC_BIT,0,0};
  }
  if(!strcmp(name,"dpxl")){result.kind=WC_DIRECT;return result;}
  if(strlen(name)==3 && name[0]=='a' && name[1]=='r' && name[2]>='0' && name[2]<='7') {
    result.kind=WC_DIRECT;return result;
  }
  memmap *spaces[]={data,sfr,sfrbit,bit};
  for(unsigned i=0;i<sizeof spaces/sizeof spaces[0];++i)if(spaces[i])
    for(const set *node=spaces[i]->syms;node;node=node->next) {
      const symbol *s=node->item;
      if(!s || !s->rname[0] || getSize(s->type)<=0)continue;
      if(bc_symbol_offset(name,s->rname,(unsigned)getSize(s->type))<0)continue;
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
mcs251BackendCost(const char *text,size_t length,bool bodyEstimate)
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
    unsigned bytes=bc_instruction_bytes_mode(p,n,mcs251BackendSymbol,NULL,bodyEstimate);
    if(!bytes || (!bodyEstimate && ended)){++c.unknown;continue;}
    char op[24],args[512];size_t k=0,j=0;
    while(k<n && !isspace((unsigned char)temp[k]) && k+1<sizeof op){op[k]=(char)tolower((unsigned char)temp[k]);++k;}
    op[k]=0;
    for(;k<n;++k)if(!isspace((unsigned char)temp[k]))args[j++]=temp[k];args[j]=0;
    char *second=strchr(args,',');if(second)*second++=0;
    if(!bodyEstimate && ((op[0]=='j') || !strcmp(op,"sjmp") || !strcmp(op,"ljmp") || !strcmp(op,"ejmp") || !strcmp(op,"djnz") || !strcmp(op,"cjne"))){++c.unknown;continue;}
    c.bytes+=bytes;++c.instructions;
    if(bodyEstimate)continue;
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
  if(!bodyEstimate && (returns!=1 || depth))++c.unknown;
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
