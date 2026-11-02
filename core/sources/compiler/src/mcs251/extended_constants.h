/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅跟踪 MCS-251 非内存映射、非换组的物理字节 16..31。
 * DPX/SPX、A/B、低寄存器组不在证明域内。只删同值的立即数 MOV；
 * 不删存储、不重排 volatile、不把同地址读取当成同值读取。
 */
#ifndef MX251_EXTENDED_CONSTANTS_H
#define MX251_EXTENDED_CONSTANTS_H
#include "instruction_effects.h"
#include <errno.h>

typedef struct { unsigned char value[32]; uint32_t known; } mx251_extended_constants;
static inline void mx251ExtendedReset(mx251_extended_constants *s) { s->known=0; }

/* 解析失败即清空事实。源文本只来自编译器发射器，内联汇编由外层拒绝。 */
static inline int mx251ExtendedApply(mx251_extended_constants *s,const char *line)
{
  char op[24],args[256],*a,*b; size_t p=0,n=0;
  while(isspace((unsigned char)line[p]))++p;
  if(!line[p] || line[p]==';')return 0;
  while(line[p] && !isspace((unsigned char)line[p]) && line[p]!=';') {
    if(n+1>=sizeof op)goto barrier;
    op[n++]=(char)tolower((unsigned char)line[p++]);
  }
  op[n]=0;n=0;
  while(line[p] && line[p]!=';') {
    if(!isspace((unsigned char)line[p])) {
      if(n+1>=sizeof args)goto barrier;
      args[n++]=line[p];
    }
    ++p;
  }
  args[n]=0;a=args;b=strchr(a,',');if(b)*b++=0;
  if(strchr(op,':') || (b && strchr(b,',')))goto barrier;
  if(!strcmp(op,"mov")) {
    mx251_effect_operand dst,src;
    if(!b || !*b)goto barrier;
    if(mx251EffectRegister(a,&dst)) {
      uint32_t mask=dst.gpr & UINT32_C(0xffff0000);
      if(!mask)return 0; /* A/B/DPX/SPX/低寄存器写不能覆盖跟踪域。 */
      if(*b=='#' && isdigit((unsigned char)b[1]) && (dst.width==2 || dst.width==4)) {
        char *end;errno=0;unsigned long value=strtoul(b+1,&end,0);
        if(!errno && !*end && value<=65535 && mask==dst.gpr) {
          unsigned first=0;while(!(mask&(UINT32_C(1)<<first)))++first;
          int equal=(s->known&mask)==mask;
          for(unsigned i=0;i<dst.width;++i) {
            unsigned char byte=(unsigned char)(value >> (8*(dst.width-1-i)));
            if(equal && s->value[first+i]!=byte)equal=0;
            s->value[first+i]=byte;
          }
          s->known|=mask;return equal;
        }
      }
      /* 一切其它写法只杀死准确的物理目标切片，含重叠 WR/DR。 */
      s->known&=~mask;return 0;
    }
    /* 非寄存器目的地仅接受编译器常见的内存 MOV 形态；不推断
     * 内存内容或低寄存器别名。16..31 不在 RAM/SFR 映射中。 */
    if(a[0]=='@') {
      if(!mx251EffectOperand(a,&dst) || !dst.indirect)goto barrier;
    } else if(a[0]!='_' && a[0]!='(' && !isdigit((unsigned char)a[0]) &&
              strcmp(a,"dpxl") && strcmp(a,"dpl") && strcmp(a,"dph") &&
              strcmp(a,"sp") && strcmp(a,"sph"))goto barrier;
    if(!mx251EffectOperand(b,&src) || src.indirect)goto barrier;
    return 0;
  }
  /* 跳转／调用／返回不跨越；没有支配关系证明不能沿用常量。 */
  if(op[0]=='j' || strstr(op,"call") || strstr(op,"ret") ||
     !strcmp(op,"djnz") || !strcmp(op,"cjne"))goto barrier;
  mx251_effects e=mx251InstructionEffects(line,strlen(line),"",0);
  if(e.unknown || e.calls)goto barrier;
  s->known&=~e.writes;return 0;
barrier:
  mx251ExtendedReset(s);return 0;
}
#endif
