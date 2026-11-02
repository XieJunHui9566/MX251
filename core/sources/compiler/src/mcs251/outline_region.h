/* Conservative post-allocation region grammar. GPL-2.0-or-later. */
#ifndef MCS251_OUTLINE_REGION_H
#define MCS251_OUTLINE_REGION_H
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define M251_OUTLINE_TEXT 512
/* Short repeated regions still require >=3 copies and >63 B guaranteed
 * aggregate saving. Keep every existing opcode/volatile/stack guard. */
#define M251_OUTLINE_MIN 8
#define M251_OUTLINE_MAX 96

/* 只接受已由 emitter 产生的寄存器/普通间接访存；不是汇编器。
 * 直接地址、位地址、PC 相对表达式与任意未知写操作均构成屏障。 */
static int m251OutlineReg (const char *s)
{
  const char *p=s; char *end; long n;
  if (!strcmp(s,"a") || !strcmp(s,"b") || !strcmp(s,"c") ||
      !strcmp(s,"dpl") || !strcmp(s,"dph") || !strcmp(s,"dpxl") ||
      !strcmp(s,"dptr") || !strcmp(s,"dpx")) return 1;
  if (*p=='w' || *p=='d') ++p;
  if (*p++!='r' || !isdigit((unsigned char)*p)) return 0;
  n=strtol(p,&end,10);
  if (*end || n<0 || n>31) return 0;
  if (*s=='w') return !(n&1) && n<=30;
  if (*s=='d') return !(n&3) && n<=28;
  return n<=15;
}

/* 字节视图单独建模，不能让 SFR 读取意外接受 WR/DR 或 SPX。
 * ARn 是 emitter 的 direct-byte 别名；绝不作为 @ARn 间接寄存器。 */
static int m251OutlineByteReg (const char *s)
{
  return !strcmp(s,"a") || !strcmp(s,"b") || !strcmp(s,"dpl") ||
         !strcmp(s,"dph") || !strcmp(s,"dpxl") || (*s=='r' && m251OutlineReg(s));
}
static int m251OutlineByteAlias (const char *s)
{
  return s[0]=='a' && s[1]=='r' && s[2]>='0' && s[2]<='7' && !s[3];
}

/* 每个 helper 只多一层 24 位返回地址，不调用其它 helper。
 * 保持原物理栈地址：SPX' = SPX + 3, displacement' = displacement - 3。
 * 只接受非正的 16 位位移；禁止访问新增返回槽和越界重编码。 */
static int m251OutlineOperand (const char *s, char *out)
{
  size_t n=strlen(s);
  if (!n || n>=M251_OUTLINE_TEXT/2) return 0;
  if (!strncmp(s,"@spx",4))
    {
      long d=0; char *end;
      /* 只改已有的 indexed 编码，避免无位移形式变长破坏收益下界。 */
      if (!s[4]) return 0;
      if (s[4])
        {
          if (s[4]!='-' || !s[5]) return 0;
          d=strtol(s+5,&end,0);
          if (*end || d<0 || d>32765) return 0;
        }
      snprintf(out,M251_OUTLINE_TEXT/2,"@spx-0x%04lx",d+3);
      return 1;
    }
  if (*s=='#')
    {
      if (!s[1]) return 0;
      for (const char *p=s+1;*p;++p)
        if (!isalnum((unsigned char)*p) && !strchr("_()+-*/<>&|^~",*p)) return 0;
    }
  else if (*s=='@')
    {
      if (!strcmp(s,"@a+dptr")) { strcpy(out,s); return 1; }
      if (!m251OutlineReg(s+1) || !strcmp(s+1,"a") || !strcmp(s+1,"b") ||
          !strcmp(s+1,"c") || !strcmp(s+1,"dpl") || !strcmp(s+1,"dph") ||
          !strcmp(s+1,"dpxl")) return 0;
    }
  else if (!m251OutlineReg(s)) return 0;
  strcpy(out,s); return 1;
}

static int m251OutlineParseRead (const char *line, char *key, char *body,
                                 const char *readSymbol, int readBit)
{
  char op[16], args[M251_OUTLINE_TEXT], a[M251_OUTLINE_TEXT/2], b[M251_OUTLINE_TEXT/2];
  const char *p=line; unsigned n=0; char *comma;
  while (isspace((unsigned char)*p)) ++p;
  while (isalpha((unsigned char)*p) && n<sizeof(op)-1) op[n++]=*p++;
  op[n]=0;
  if (!isspace((unsigned char)*p)) return 0;
  while (isspace((unsigned char)*p)) ++p;
  n=0;
  for (;*p && *p!=';';++p)
    if (!isspace((unsigned char)*p))
      { if (n>=sizeof(args)-1) return 0; args[n++]=*p; }
  args[n]=0;
  if (strlen(op)+n+2>=M251_OUTLINE_TEXT) return 0;
  snprintf(key,M251_OUTLINE_TEXT,"%s %s",op,args);
  if ((!strcmp(op,"mul") || !strcmp(op,"div")) && !strcmp(args,"ab"))
    { strcpy(body,key); return 1; }
  comma=strchr(args,',');
  if (comma)
    {
      *comma++=0;
      /* 只允许当前 IR 证明的单个 SFR 源；不重排、不删读、不开放写入。
       * 原生字节 Rn 和 A 的读取保持原样；位读取仍只允许 C。 */
      if (readSymbol && !strcmp(op,"mov") && !strcmp(comma,readSymbol) &&
          (readBit ? !strcmp(args,"c") : m251OutlineByteReg(args)))
        { strcpy(body,key); return 1; }
      /* 此路径只在 bank 0 普通函数启用，helper 位于同一函数的 equate
       * 作用范围内。白名单保持已发射的 direct-byte 指令，不改成 Rn。
       * 位操作、宽操作、间接寻址以及 PUSH/POP 均不借此开放。 */
      if (m251OutlineByteAlias(args) || m251OutlineByteAlias(comma))
        {
          int left=m251OutlineByteAlias(args), right=m251OutlineByteAlias(comma);
          int imm=*comma=='#' && m251OutlineOperand(comma,b);
          int valid=0;
          if (!strcmp(op,"mov"))
            valid=(left && (right || m251OutlineByteReg(comma) || imm)) ||
                  (right && m251OutlineByteReg(args));
          else if (!strcmp(op,"anl") || !strcmp(op,"orl") || !strcmp(op,"xrl"))
            valid=(left && (!strcmp(comma,"a") || imm)) || (right && !strcmp(args,"a"));
          else if (!strcmp(op,"add") || !strcmp(op,"addc") || !strcmp(op,"subb"))
            valid=right && !strcmp(args,"a");
          if (!valid) return 0;
          strcpy(body,key); return 1;
        }
      if (strchr(comma,',') || !m251OutlineOperand(args,a) || !m251OutlineOperand(comma,b)) return 0;
      if (strcmp(op,"mov") && strcmp(op,"movz") && strcmp(op,"movs") && strcmp(op,"movc") &&
          strcmp(op,"add") && strcmp(op,"addc") && strcmp(op,"sub") && strcmp(op,"subb") &&
          strcmp(op,"anl") && strcmp(op,"orl") && strcmp(op,"xrl") && strcmp(op,"cmp") &&
          strcmp(op,"inc") && strcmp(op,"dec") && strcmp(op,"mul") && strcmp(op,"div")) return 0;
      if (*args=='#' || (!strcmp(op,"movc") && (strcmp(args,"a") || strcmp(comma,"@a+dptr")))) return 0;
      snprintf(body,M251_OUTLINE_TEXT,"%s %s,%s",op,a,b);
    }
  else
    {
      if (strcmp(op,"inc") && strcmp(op,"dec") && strcmp(op,"clr") && strcmp(op,"cpl") &&
          strcmp(op,"rl") && strcmp(op,"rlc") && strcmp(op,"rr") && strcmp(op,"rrc") &&
          strcmp(op,"swap")) return 0;
      if (!m251OutlineReg(args) &&
          !(m251OutlineByteAlias(args) && (!strcmp(op,"inc") || !strcmp(op,"dec")))) return 0;
      snprintf(body,M251_OUTLINE_TEXT,"%s %s",op,args);
    }
  return 1;
}
static int m251OutlineParse (const char *line, char *key, char *body)
{ return m251OutlineParseRead(line,key,body,NULL,0); }
#endif
