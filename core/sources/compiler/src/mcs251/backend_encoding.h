/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅用于同一 IR 的完整后端选择。补齐普通后端已生成的直接寻址成本，
 * 不改变宽后端内部优化的成本或放宽其候选准入。符号空间来自类型表。 */
#ifndef MCS251_BACKEND_ENCODING_H
#define MCS251_BACKEND_ENCODING_H
#include "wide_cost.h"
static unsigned
bc_instruction_bytes(const char *text,size_t length,wide_cost_resolver resolver,const void *ctx)
{
  unsigned old=wide_instruction_bytes_context(text,length,resolver,ctx);
  if(old)return old;
  char op[16],a[256],b[256];size_t k=0,x=0,y=0;int second=0;
  const char *p=text,*end=text+length;
  while(p<end && isspace((unsigned char)*p))++p;
  while(p<end && isalpha((unsigned char)*p)) {
    if(k+1>=sizeof op)return 0;op[k++]=(char)tolower((unsigned char)*p++);
  }
  op[k]=0;if(!k || (p<end && !isspace((unsigned char)*p)))return 0;
  for(;p<end && *p!=';';++p) {
    if(isspace((unsigned char)*p))continue;
    if(*p==','){if(second)return 0;second=1;continue;}
    if(second){if(y+1>=sizeof b)return 0;b[y++]=*p;}
    else {if(x+1>=sizeof a)return 0;a[x++]=*p;}
  }
  a[x]=b[y]=0;
  wide_cost_operand dst=wide_cost_arg(a),src=wide_cost_arg(b);
  if(dst.kind==WC_UNKNOWN && resolver)dst=resolver(a,ctx);
  if(src.kind==WC_UNKNOWN && resolver)src=resolver(b,ctx);
  if(!strcmp(op,"anl") || !strcmp(op,"orl") || !strcmp(op,"xrl")) {
    if(dst.kind!=WC_DIRECT || !second)return 0;
    if(src.kind==WC_IMM)return 3;
    if(src.kind==WC_REG && src.width==1 && src.reg==11)return 2;
  }
  if(!strcmp(op,"inc") || !strcmp(op,"dec"))
    return dst.kind==WC_DIRECT && (!second || !strcmp(b,"#1"))?2:0;
  if(!strcmp(op,"add"))
    return second && dst.kind==WC_REG && dst.width==1 && dst.reg==11 && src.kind==WC_DIRECT?2:0;
  return 0;
}
#endif
