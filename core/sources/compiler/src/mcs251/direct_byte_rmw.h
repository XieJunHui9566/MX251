/* SPDX-License-Identifier: GPL-2.0-or-later
 * 完整候选选定后，折叠普通 DATA 字节的 load/op/store。寄存器分配、
 * 栈和调用保持原状；只在临时寄存器后续不读时省去它的写入。
 * SFR/端口、绝对地址、未知存储空间及 A/B 不进入本规则。
 * TSC80251 手册：MOV 不改 CY/AC/OV/N/Z；两种字节 ALU 形式均按同一
 * 结果更新 N/Z，保留 CY/AC/OV。故不依赖“flags 已死”的推断。 */
#ifndef MCS251_DIRECT_BYTE_RMW_H
#define MCS251_DIRECT_BYTE_RMW_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "instruction_effects.h"
#include "backend_encoding.h"
enum {DRMW_LINES=8192,DRMW_ARG=192};
typedef bool (*drmw_memory)(const char *,const void *);
typedef struct {drmw_memory memory;const void *ctx;} drmw_context;
typedef struct {const char *raw;size_t length;char op[20],a[DRMW_ARG],b[DRMW_ARG];bool label,empty,valid,removed;} drmw_line;
static bool drmw_parse(drmw_line *l,const char *raw,size_t length)
{
 memset(l,0,sizeof *l);l->raw=raw;l->length=length;l->valid=true;
 const char *p=raw,*end=raw+length;while(p<end && isspace((unsigned char)*p))++p;
 const char *comment=memchr(p,';',(size_t)(end-p));if(comment)end=comment;
 while(end>p && isspace((unsigned char)end[-1]))--end;
 if(p==end){l->empty=true;return true;}
 if(end[-1]==':'){l->label=true;return true;}
 size_t k=0;while(p<end && isalpha((unsigned char)*p)) {
  if(k+1>=sizeof l->op)return l->valid=false;l->op[k++]=(char)tolower((unsigned char)*p++);
 }
 if(!k || (p<end && !isspace((unsigned char)*p)))return l->valid=false;
 unsigned which=0;size_t n=0;
 for(;p<end;++p) {
  if(isspace((unsigned char)*p))continue;
  if(*p==','){if(which || !n)return l->valid=false;which=1;n=0;continue;}
  if(n+1>=DRMW_ARG)return l->valid=false;(which?l->b:l->a)[n++]=*p;
 }
 return true;
}
static int drmw_next(const drmw_line *ls,unsigned count,unsigned i)
{for(++i;i<count;++i)if(!ls[i].empty && !ls[i].removed)return (int)i;return -1;}
static bool drmw_register_dead(const drmw_line *ls,unsigned count,unsigned start,uint32_t mask,drmw_memory memory,const void *ctx)
{
 for(unsigned i=start+1;i<count;++i) {
  const drmw_line *l=&ls[i];if(l->empty || l->removed)continue;
  if(!l->valid || l->label)return false;
  if(!strcmp(l->op,"eret"))return !l->a[0] && !l->b[0];
  if(l->op[0]=='j' || !strcmp(l->op,"sjmp") || !strcmp(l->op,"ljmp") || !strcmp(l->op,"ejmp") ||
     !strcmp(l->op,"djnz") || !strcmp(l->op,"cjne") || !strcmp(l->op,"ecall"))return false;
  mx251_effects e=mx251InstructionEffects(l->raw,l->length,NULL,0);
  if(e.unknown) {
   mx251_effect_operand r;
   /* 已分配的普通 C DATA 对象不是物理寄存器或 SFR 别名。 */
   if(!strcmp(l->op,"mov") && memory(l->b,ctx) && mx251EffectRegister(l->a,&r)) {e.reads=0;e.writes=r.gpr;e.unknown=0;}
   else if(!strcmp(l->op,"mov") && memory(l->a,ctx) && mx251EffectRegister(l->b,&r)) {e.reads=r.gpr;e.writes=0;e.unknown=0;}
   else if(memory(l->a,ctx) && ((!strcmp(l->op,"inc") || !strcmp(l->op,"dec")) && !l->b[0])) {e.reads=e.writes=0;e.unknown=0;}
   else if(memory(l->a,ctx) && (!strcmp(l->op,"anl") || !strcmp(l->op,"orl") || !strcmp(l->op,"xrl")) && l->b[0]=='#') {e.reads=e.writes=0;e.unknown=0;}
  }
  if(e.unknown || e.reads&mask)return false;
  if((e.writes&mask)==mask)return true;
 }
 return false;
}
static wide_cost_operand drmw_cost_symbol(const char *s,const void *opaque)
{
 const drmw_context *c=opaque;
 wide_cost_operand a={WC_UNKNOWN,0,0};if(c->memory(s,c->ctx))a.kind=WC_DIRECT;return a;
}
static bool drmw_select(drmw_line *ls,unsigned count,unsigned i,drmw_memory memory,const void *ctx,unsigned *jout,unsigned *kout,unsigned *before,unsigned *after,char *replacement,size_t capacity)
{
 drmw_line *a=&ls[i];if(!a->valid || a->label || a->empty || a->removed || strcmp(a->op,"mov") || !memory(a->b,ctx))return false;
 mx251_effect_operand reg;if(!mx251EffectRegister(a->a,&reg) || reg.width!=1 || !reg.gpr || reg.gpr&(UINT32_C(3)<<10))return false;
 int j=drmw_next(ls,count,i);if(j<0)return false;int k=drmw_next(ls,count,(unsigned)j);if(k<0)return false;
 drmw_line *b=&ls[j],*c=&ls[k];if(!b->valid || !c->valid || b->label || c->label || strcmp(a->a,b->a) || strcmp(c->op,"mov") || strcmp(a->b,c->a) || strcmp(a->a,c->b))return false;
 bool unary=!strcmp(b->op,"inc") || !strcmp(b->op,"dec");
 if(unary) {if(b->b[0] && strcmp(b->b,"#1"))return false;}
 else if((strcmp(b->op,"anl") && strcmp(b->op,"orl") && strcmp(b->op,"xrl")) || b->b[0]!='#' || !b->b[1])return false;
 if(!drmw_register_dead(ls,count,(unsigned)k,reg.gpr,memory,ctx))return false;
 int n=snprintf(replacement,capacity,"\t%s\t%s%s%s\n",b->op,a->b,unary?"":",",unary?"":b->b);
 if(n<0 || (size_t)n>=capacity)return false;
 drmw_context resolver={memory,ctx};
 unsigned x=bc_instruction_bytes(a->raw,a->length,drmw_cost_symbol,&resolver),y=bc_instruction_bytes(b->raw,b->length,drmw_cost_symbol,&resolver),z=bc_instruction_bytes(c->raw,c->length,drmw_cost_symbol,&resolver);
 *before=x+y+z;*after=bc_instruction_bytes(replacement,(size_t)n,drmw_cost_symbol,&resolver);
 if(!x || !y || !z || !*after || *after>=*before)return false;
 *jout=(unsigned)j;*kout=(unsigned)k;return true;
}
#endif
