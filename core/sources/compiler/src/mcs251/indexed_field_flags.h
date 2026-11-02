/* GPL-2.0-or-later. 真实最终发射序列中的局部 flags 覆盖证明，不是周期模型。 */
#ifndef MCS251_INDEXED_FIELD_FLAGS_H
#define MCS251_INDEXED_FIELD_FLAGS_H
#include <stdbool.h>
#include "wide_cost.h"
/* N=1 Z=2 CY=4 OV=8 AC=16。地址 INC/ANL 不写 A，故不产生 P 差异。
 * 每条允许的指令均不读取尚不同的 flags；控制流、内存符号和未知形式
 * 在覆盖完成前拒绝。AC 只由8位 ADD/SUB/CMP 更新（TSC Rev E表4.4）。 */
static bool
wide_indexed_flags_safe(const char *text,size_t length,unsigned *siteCount)
{
  unsigned pending=0,sites=0,walked=0;
  const char marker[]="; wide native-indexed-field flags=";
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    const char *p=text+at,*last=text+end;at=end+1;
    while(p<last && isspace((unsigned char)*p))++p;
    if((size_t)(last-p)>sizeof marker-1 && !memcmp(p,marker,sizeof marker-1)) {
      const char *q=p+sizeof marker-1;unsigned flags=0;
      while(q<last && isdigit((unsigned char)*q)) {if(flags>100)return false;flags=flags*10u+(*q++-'0');}
      if(q==last || *q!=' ' || (flags!=3 && flags!=15))return false;
      pending|=flags;++sites;walked=0;continue;
    }
    if(!pending || p==last || *p==';')continue;
    if(++walked>64)return false;
    char op[16],args[192];size_t n=0;
    while(p<last && isalpha((unsigned char)*p)) {if(n+1>=sizeof op)return false;op[n++]=(char)tolower((unsigned char)*p++);}
    op[n]=0;if(!n || p==last || !isspace((unsigned char)*p))return false;
    n=0;while(p<last && *p!=';') {if(!isspace((unsigned char)*p)) {if(n+1>=sizeof args)return false;args[n++]=(char)tolower((unsigned char)*p);}++p;}
    args[n]=0;char *b=strchr(args,',');if(b){*b++=0;if(strchr(b,','))return false;}else b=args+n;
    wide_cost_operand a=wide_cost_arg(args),other=wide_cost_arg(b);
    if(!strcmp(op,"mov")) {
      /* 寄存器/普通间接地址/立即数，不接纳 C/PSW/SFR 或其它符号观察者。 */
      bool valid=(a.kind==WC_REG && (other.kind==WC_REG || other.kind==WC_IMM || other.kind==WC_INDIRECT || other.kind==WC_INDEXED)) ||
        ((a.kind==WC_INDIRECT || a.kind==WC_INDEXED) && other.kind==WC_REG);
      if(!valid)return false;
    } else if(!strcmp(op,"movz") || !strcmp(op,"movs")) {
      if(a.kind!=WC_REG || other.kind!=WC_REG || a.width!=2 || other.width!=1)return false;
    } else if(!strcmp(op,"cmp") || !strcmp(op,"add") || !strcmp(op,"sub") ||
              !strcmp(op,"anl") || !strcmp(op,"orl") || !strcmp(op,"xrl")) {
      if(a.kind!=WC_REG || (other.kind!=WC_REG && other.kind!=WC_IMM))return false;
      bool arithmetic=!strcmp(op,"cmp") || !strcmp(op,"add") || !strcmp(op,"sub");
      if(!arithmetic && a.width>2)return false;
      pending&=~(arithmetic?(15u|(a.width==1?16u:0u)):3u);
    } else if(!strcmp(op,"inc") || !strcmp(op,"dec")) {
      if(a.kind!=WC_REG || (*b && other.kind!=WC_IMM))return false;
      pending&=~3u;
    } else return false;
  }
  if(siteCount)*siteCount=sites;
  return !pending && sites>0;
}
#endif
