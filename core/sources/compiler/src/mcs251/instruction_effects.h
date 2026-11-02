/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MX251_INSTRUCTION_EFFECTS_H
#define MX251_INSTRUCTION_EFFECTS_H
#include "register_facts.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* v1 是 may-read/may-write 上界，不是净破坏集合或寄存器分配承诺。
 * 未描述的形式置 top，不能把解析失败解释为没有副作用。
 * flags 暂按整体保守处理；内存写除已知 SPX 槽外不推断无寄存器别名。 */
enum { MX251_EFFECTS_SCHEMA=1, MX251_SPECIAL_DPTR=1, MX251_SPECIAL_DPX=3,
       MX251_SPECIAL_SPX=4, MX251_MEMORY_READ=1, MX251_MEMORY_WRITE=2 };
typedef struct {
  uint32_t reads,writes,specialReads,specialWrites,flagsReads,flagsWrites;
  unsigned memory,instructions,unknown,calls;
} mx251_effects;
typedef struct { uint32_t gpr,special;unsigned width;int indirect,immediate,stack; } mx251_effect_operand;

static inline void mx251EffectsUnknown(mx251_effects *e) {
  e->reads=e->writes=e->specialReads=e->specialWrites=e->flagsReads=e->flagsWrites=UINT32_MAX;
  e->memory=MX251_MEMORY_READ|MX251_MEMORY_WRITE;++e->unknown;
}
static inline void mx251EffectsMerge(mx251_effects *a,const mx251_effects *b) {
  a->reads|=b->reads;a->writes|=b->writes;a->specialReads|=b->specialReads;a->specialWrites|=b->specialWrites;
  a->flagsReads|=b->flagsReads;a->flagsWrites|=b->flagsWrites;a->memory|=b->memory;
  a->instructions+=b->instructions;a->unknown+=b->unknown;a->calls+=b->calls;
}
static inline int mx251EffectRegister(const char *s,mx251_effect_operand *o) {
  unsigned first,width=1;char *end;
  memset(o,0,sizeof *o);
  if(!strcmp(s,"a") || !strcmp(s,"acc"))o->gpr=UINT32_C(1)<<MCS251_BYTE_A;
  else if(!strcmp(s,"b"))o->gpr=UINT32_C(1)<<MCS251_BYTE_B;
  else if(!strcmp(s,"dptr") || !strcmp(s,"dpl") || !strcmp(s,"dph"))o->special=MX251_SPECIAL_DPTR;
  else if(!strcmp(s,"dpx"))o->special=MX251_SPECIAL_DPX;
  else if(!strcmp(s,"spx"))o->special=MX251_SPECIAL_SPX;
  else {
    if(*s=='w'){width=2;++s;}else if(*s=='d'){width=4;++s;}
    if(*s++!='r' || !isdigit((unsigned char)*s))return 0;
    unsigned long number=strtoul(s,&end,10);if(*end || number>63)return 0;first=(unsigned)number;
    if(mcs251RegisterEncodable(first,width))o->gpr=mcs251RegisterBytes(first,width);
    else if((width==4 && first==56) || (width==2 && first==56))o->special=MX251_SPECIAL_DPX;
    else if(width==2 && first==58)o->special=MX251_SPECIAL_DPTR;
    else if((width==4 && first==60) || (width==2 && (first==60 || first==62)))o->special=MX251_SPECIAL_SPX;
    else return 0;
  }
  o->width=width;return 1;
}
static inline int mx251EffectOperand(const char *s,mx251_effect_operand *o) {
  char lowered[192];size_t j=0;
  for(;s[j];++j){if(j+1>=sizeof lowered)return 0;lowered[j]=(char)tolower((unsigned char)s[j]);}
  lowered[j]=0;s=lowered;
  memset(o,0,sizeof *o);
  if(*s=='#' && s[1]) {o->immediate=1;return 1;}
  if(*s!='@')return mx251EffectRegister(s,o);
  ++s;
  char name[16];size_t n=0;
  while(s[n] && s[n]!='+' && s[n]!='-') {if(n+1>=sizeof name)return 0;name[n]=s[n];++n;}
  name[n]=0;
  if(!mx251EffectRegister(name,o))return 0;
  if(s[n]) {
    /* 只接受发射器已经生成的数值 SPX 位移，不猜符号或表达式。 */
    if(o->special!=MX251_SPECIAL_SPX)return 0;
    char *end;const char *value=s+n+1;if(!*value)return 0;
    (void)strtoul(value,&end,0);if(end==value || *end)return 0;
  }
  o->indirect=1;o->stack=o->special==MX251_SPECIAL_SPX;return 1;
}
static inline void mx251EffectRead(mx251_effects *e,const mx251_effect_operand *o) {
  e->reads|=o->gpr;e->specialReads|=o->special;
  if(o->indirect)e->memory|=MX251_MEMORY_READ;
}
static inline int mx251EffectWrite(mx251_effects *e,const mx251_effect_operand *o) {
  if(o->immediate)return 0;
  if(o->indirect) {
    if(!o->stack)return 0;
    e->reads|=o->gpr;e->specialReads|=o->special;e->memory|=MX251_MEMORY_WRITE;
  } else {
    e->writes|=o->gpr;e->specialWrites|=o->special;
    if(o->gpr & (UINT32_C(1)<<MCS251_BYTE_A))e->flagsWrites=UINT32_MAX;
  }
  return 1;
}
static inline int mx251EffectLocalLabel(const char *s,const char *body,size_t length) {
  if(strncmp(s,"__wm_",5))return 0;
  size_t n=strlen(s);
  for(size_t i=0;i+n<length;++i)
    if((!i || body[i-1]=='\n') && body[i+n]==':' && !memcmp(body+i,s,n))return 1;
  return 0;
}
static inline mx251_effects mx251InstructionEffects(const char *line,size_t length,const char *body,size_t bodyLength) {
  mx251_effects e={0};char op[24],operands[192],*a,*b;size_t p=0,k=0;
  while(p<length && isspace((unsigned char)line[p]))++p;
  if(p==length || line[p]==';')return e;
  /* 本 API 分析发射器文本；只接受完整独占行标签，数据与指令同一行均拒绝。 */
  size_t end=length;while(end>p && isspace((unsigned char)line[end-1]))--end;
  if(line[end-1]==':' && !memchr(line+p,'\t',end-p) && !memchr(line+p,' ',end-p))return e;
  e.instructions=1;
  while(p<length && !isspace((unsigned char)line[p])) {
    if(k+1>=sizeof op){mx251EffectsUnknown(&e);return e;}op[k++]=(char)tolower((unsigned char)line[p++]);
  }
  op[k]=0;k=0;
  size_t operandBegin=p;
  while(p<length && line[p]!=';') {
    if(!isspace((unsigned char)line[p])) {
      if(k+1>=sizeof operands){mx251EffectsUnknown(&e);return e;}operands[k++]=line[p];
    }
    ++p;
  }
  operands[k]=0;a=operands;b=strchr(a,',');if(b)*b++=0;
  if(b && strchr(b,','))goto unknown;
  if(!strcmp(op,"ecall") || !strcmp(op,"lcall") || !strcmp(op,"acall")) {e.calls=1;goto unknown;}
  if(!strcmp(op,"ret") || !strcmp(op,"eret")) {
    if(*a || b)goto unknown;
    e.specialReads=e.specialWrites=MX251_SPECIAL_SPX;e.memory=MX251_MEMORY_READ;return e;
  }
  if(!strcmp(op,"nop")) {if(*a || b)goto unknown;return e;}
  /* 只接受本发射器的完整十六进制字面量，使用原始文本防止把空白
   * 拼成另一个数字。PUSHW只读取/更新SPX并写栈，不破坏GPR或flags。 */
  if(!strcmp(op,"pushw")) {
    const char *q=line+operandBegin,*limit=line+length;
    while(q<limit && isspace((unsigned char)*q))++q;
    if(b || limit-q<7 || q[0]!='#' || q[1]!='0' || tolower((unsigned char)q[2])!='x')goto unknown;
    for(unsigned j=3;j<7;++j)if(!isxdigit((unsigned char)q[j]))goto unknown;
    q+=7;while(q<limit && isspace((unsigned char)*q))++q;
    if(q<limit && *q!=';')goto unknown;
    e.specialReads=e.specialWrites=MX251_SPECIAL_SPX;e.memory=MX251_MEMORY_WRITE;return e;
  }
  /* 精确限定 direct ACC 与本函数已定义的局部标签。其寄存器效果
   * 只有读写 A；NZ 与随 A 改变的 P 属于 flags 写，上界接口仍按
   * 整体 flags 保守表达。其它 direct/SFR、寄存器和未定义目标未知。 */
  if(!strcmp(op,"djnz")) {
    if(strcmp(a,"acc") || !b || !mx251EffectLocalLabel(b,body,bodyLength))goto unknown;
    e.reads=e.writes=UINT32_C(1)<<MCS251_BYTE_A;
    e.flagsWrites=UINT32_MAX;return e;
  }
  if(!strcmp(op,"sjmp") || !strcmp(op,"ljmp") || !strcmp(op,"ejmp") ||
     !strcmp(op,"je") || !strcmp(op,"jne") || !strcmp(op,"jc") || !strcmp(op,"jnc") ||
     !strcmp(op,"jz") || !strcmp(op,"jnz")) {
    if(b || !mx251EffectLocalLabel(a,body,bodyLength))goto unknown;
    if(!strcmp(op,"jz") || !strcmp(op,"jnz"))e.reads=UINT32_C(1)<<MCS251_BYTE_A;
    else if(strcmp(op,"sjmp") && strcmp(op,"ljmp") && strcmp(op,"ejmp"))e.flagsReads=UINT32_MAX;
    return e;
  }
  mx251_effect_operand x,y;
  if(!mx251EffectOperand(a,&x))goto unknown;
  if(b && !mx251EffectOperand(b,&y))goto unknown;
  /* 未限定的间接访问可能落入外设空间，不能用此摘要证明调用无副作用。 */
  if((x.indirect && !x.stack) || (b && y.indirect && !y.stack))goto unknown;
  if(!strcmp(op,"mov") || !strcmp(op,"movz") || !strcmp(op,"movs")) {
    if(!b || !mx251EffectWrite(&e,&x))goto unknown;
    mx251EffectRead(&e,&y);return e;
  }
  if(!strcmp(op,"push") || !strcmp(op,"pop")) {
    if(b || x.immediate || x.indirect)goto unknown;
    e.specialReads=e.specialWrites=MX251_SPECIAL_SPX;
    if(!strcmp(op,"push")){mx251EffectRead(&e,&x);e.memory=MX251_MEMORY_WRITE;}
    else {if(!mx251EffectWrite(&e,&x))goto unknown;e.memory=MX251_MEMORY_READ;}
    return e;
  }
  if(!strcmp(op,"cmp")) {
    if(!b)goto unknown;
    mx251EffectRead(&e,&x);mx251EffectRead(&e,&y);e.flagsWrites=UINT32_MAX;return e;
  }
  if(!strcmp(op,"add") || !strcmp(op,"addc") || !strcmp(op,"sub") || !strcmp(op,"subb") ||
     !strcmp(op,"anl") || !strcmp(op,"orl") || !strcmp(op,"xrl")) {
    if(!b || !mx251EffectWrite(&e,&x))goto unknown;
    mx251EffectRead(&e,&x);mx251EffectRead(&e,&y);e.flagsReads=e.flagsWrites=UINT32_MAX;return e;
  }
  if(!strcmp(op,"inc") || !strcmp(op,"dec") || !strcmp(op,"sll") || !strcmp(op,"srl") ||
     !strcmp(op,"sra") || !strcmp(op,"rl") || !strcmp(op,"rr") || !strcmp(op,"rlc") ||
     !strcmp(op,"rrc") || !strcmp(op,"cpl")) {
    if((b && (strcmp(op,"inc") && strcmp(op,"dec"))) || (b && !y.immediate) || !mx251EffectWrite(&e,&x))goto unknown;
    mx251EffectRead(&e,&x);e.flagsReads=e.flagsWrites=UINT32_MAX;return e;
  }
  if(!strcmp(op,"clr")) {
    if(b || !mx251EffectWrite(&e,&x))goto unknown;
    return e;
  }
  if(!strcmp(op,"xch")) {
    if(!b || x.indirect || y.indirect || !mx251EffectWrite(&e,&x) || !mx251EffectWrite(&e,&y))goto unknown;
    mx251EffectRead(&e,&x);mx251EffectRead(&e,&y);return e;
  }
unknown:
  mx251EffectsUnknown(&e);return e;
}
static inline mx251_effects mx251TextEffects(const char *text,size_t length) {
  mx251_effects result={0};
  for(size_t start=0;start<length;) {
    size_t end=start;while(end<length && text[end]!='\n')++end;
    mx251_effects row=mx251InstructionEffects(text+start,end-start,text,length);
    mx251EffectsMerge(&result,&row);start=end+1;
  }
  return result;
}
#endif
