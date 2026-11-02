/* GPL-2.0-or-later. 参数化调用区域的栈字节归属，不猜测未知栈操作。 */
#ifndef MCS251_CALL_REGION_H
#define MCS251_CALL_REGION_H
#include "outline_region.h"
#define M251_CALL_REGION_STACK 256
typedef struct { int depth; unsigned char argument[M251_CALL_REGION_STACK]; int calls; } m251_call_stack;

static unsigned m251CallWidth(const char *s)
{
  if (m251OutlineByteAlias(s) || m251OutlineByteReg(s)) return 1;
  if (s[0]=='w' && m251OutlineReg(s)) return 2;
  if (s[0]=='d' && s[1]=='r' && m251OutlineReg(s)) return 4;
  return 0;
}
static int m251CallStackPush(m251_call_stack *s,unsigned width,int argument)
{
  if (!width || s->depth+(int)width>M251_CALL_REGION_STACK || (argument && s->calls)) return 0;
  for (unsigned i=0;i<width;++i) s->argument[s->depth++]=argument!=0;
  return 1;
}
static int m251CallStackPop(m251_call_stack *s,unsigned width)
{
  if (!width || (int)width>s->depth) return 0;
  for (unsigned i=0;i<width;++i)
    if (!s->calls && s->argument[s->depth-1-(int)i]) return 0;
  s->depth-=width;return 1;
}
static int m251CallStackInvoke(m251_call_stack *s,unsigned arguments)
{
  if (s->calls || arguments>(unsigned)s->depth) return 0;
  for (int i=0;i<s->depth;++i)
    if (s->argument[i]!=(i>=s->depth-(int)arguments)) return 0;
  s->calls=1;return 1;
}
static int m251CallStackOperand(const char *p,unsigned width,int depth,char *out,unsigned *growth)
{
  long d=0;char *end;
  if (strncmp(p,"@spx",4) || !width) return 0;
  if (p[4]) {
    if (p[4]!='-' || !p[5]) return 0;
    d=strtol(p+5,&end,0);if (*end || d<0 || d>32768) return 0;
    d=-d;
  }
  long low=depth+d, high=low+width-1;
  if (high>depth || (low<=0 && high>0)) return 0;
  /* helper 的返回地址插在原帧和区域新建栈槽之间。 */
  if (high<=0) { d-=3;if (!p[4]) *growth+=2; }
  if (d< -32768 || d>0) return 0;
  if (d) snprintf(out,M251_OUTLINE_TEXT/2,"@spx-0x%04lx",-d);
  else strcpy(out,"@spx");
  return 1;
}
static int m251CallRegionLine(const char *line,m251_call_stack *s,int argument,
                             char *key,char *body,unsigned *growth)
{
  char op[16],args[M251_OUTLINE_TEXT],a[M251_OUTLINE_TEXT/2],b[M251_OUTLINE_TEXT/2];
  unsigned n=0;const char *p=line;char *comma;
  while (isspace((unsigned char)*p)) ++p;
  while (isalpha((unsigned char)*p) && n<sizeof(op)-1) op[n++]=*p++;
  op[n]=0;if (!isspace((unsigned char)*p)) return 0;
  n=0;for (;*p && *p!=';';++p) if (!isspace((unsigned char)*p)) {
    if (n+1>=sizeof args) return 0;args[n++]=*p;
  }
  args[n]=0;if (strlen(op)+n+2>=M251_OUTLINE_TEXT) return 0;
  snprintf(key,M251_OUTLINE_TEXT,"%s %s",op,args);strcpy(body,key);
  if (!strcmp(op,"push") || !strcmp(op,"pop")) {
    unsigned width=m251CallWidth(args);
    if (!width) return 0;
    return !strcmp(op,"push") ? m251CallStackPush(s,width,argument) : m251CallStackPop(s,width);
  }
  /* PUSHW literal is two bytes; never reinterpret PUSH #imm as two bytes. */
  if (!strcmp(op,"pushw")) {
    if (*args!='#' || !m251OutlineOperand(args,a)) return 0;
    return m251CallStackPush(s,2,argument);
  }
  if (!strncmp(args,"spx,#",5)) {
    if (strcmp(op,"dec") || !s->calls) return 0;
    unsigned long width=strtoul(args+5,&comma,0);
    if (*comma || comma==args+5 || width>M251_CALL_REGION_STACK) return 0;
    return m251CallStackPop(s,(unsigned)width);
  }
  if (strstr(args,"@spx")) {
    if (strcmp(op,"mov") || !(comma=strchr(args,',')) || strchr(comma+1,',')) return 0;
    *comma++=0;
    int left=!strncmp(args,"@spx",4),right=!strncmp(comma,"@spx",4);
    if (left==right) return 0;
    unsigned width=m251CallWidth(left?comma:args);if (!width) return 0;
    if (!m251CallStackOperand(left?args:comma,width,s->depth,left?a:b,growth)) return 0;
    strcpy(left?b:a,left?comma:args);
    snprintf(body,M251_OUTLINE_TEXT,"mov %s,%s",a,b);return 1;
  }
  return m251OutlineParse(line,key,body);
}
#endif
