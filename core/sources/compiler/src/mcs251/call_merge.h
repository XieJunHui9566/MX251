/* Conservative adjacent caller-save coalescing. GPL-2.0-or-later. */
#ifndef MCS251_CALL_MERGE_H
#define MCS251_CALL_MERGE_H
#include <ctype.h>
#include <string.h>
enum { M251_CM_STOP, M251_CM_POP, M251_CM_PUSH, M251_CM_MOVE, M251_CM_CALL };
typedef struct { unsigned kind, mask, width; } mcs251_call_merge_op;

static void mcs251CallMergeSpace (const char **p)
{ while (isspace ((unsigned char)**p)) ++*p; }

/* 只接受已知寄存器和字面量；内存、位操作、SP/PSW、间接寻址均为边界。
 * R0..7/AR0..7 仅由桥接层在 bank 0 启用。DPTR 切片不与保存掩码重叠。 */
static int mcs251CallMergeReg (const char **text, unsigned *mask, unsigned *width)
{
  const char *p=*text; unsigned n=0, digits=0, w;
  mcs251CallMergeSpace (&p);
  const struct { const char *name; unsigned first, width; } aliases[] = {
    {"dptr",16,2},{"dpl",17,1},{"dph",16,1},{"dpxl",19,1},
    {"acc",11,1},{"a",11,1},{"b",10,1}};
  for (unsigned i=0; i<sizeof aliases/sizeof *aliases; ++i)
    {
      unsigned len=strlen (aliases[i].name);
      if (!strncmp (p,aliases[i].name,len) && !isalnum ((unsigned char)p[len]) && p[len]!='_')
        { *mask=((1u<<aliases[i].width)-1)<<aliases[i].first;
          *width=aliases[i].width; *text=p+len; return 1; }
    }
  if (p[0]=='a' && p[1]=='r') { w=1; p+=2; }
  else if (*p=='r') { w=1; ++p; }
  else if (p[0]=='w' && p[1]=='r') { w=2; p+=2; }
  else if (p[0]=='d' && p[1]=='r') { w=4; p+=2; }
  else return 0;
  while (isdigit ((unsigned char)*p))
    { if (++digits>2) return 0; n=10*n+(*p++-'0'); }
  if (!digits || n%w || n+w>16 || isalnum ((unsigned char)*p) || *p=='_') return 0;
  *mask=((1u<<w)-1)<<n; *width=w; *text=p; return 1;
}

static mcs251_call_merge_op mcs251CallMergeParse (const char *text)
{
  mcs251_call_merge_op op={M251_CM_STOP,0,0}; const char *p=text;
  unsigned other, width; mcs251CallMergeSpace (&p);
  if ((!strncmp(p,"pop",3) && isspace((unsigned char)p[3])) ||
      (!strncmp(p,"push",4) && isspace((unsigned char)p[4])))
    {
      unsigned kind=p[1]=='o'?M251_CM_POP:M251_CM_PUSH; p+=kind==M251_CM_POP?3:4;
      if (!mcs251CallMergeReg(&p,&op.mask,&op.width) || (op.mask&~0xf3ffu)) return (mcs251_call_merge_op){0,0,0};
      mcs251CallMergeSpace(&p);
      if (!*p || *p==';') op.kind=kind;
      return op;
    }
  if (strncmp(p,"mov",3) || !isspace((unsigned char)p[3])) return op;
  p+=3;
  if (!mcs251CallMergeReg(&p,&op.mask,&op.width)) return (mcs251_call_merge_op){0,0,0};
  mcs251CallMergeSpace(&p); if (*p++!=',') return (mcs251_call_merge_op){0,0,0};
  mcs251CallMergeSpace(&p);
  if (*p=='#')
    {
      /* 接受纯整数立即数，拒绝可能含 SP/代码位置的表达式及符号。 */
      ++p; if (*p=='-' || *p=='+') ++p;
      if (p[0]=='0' && (p[1]=='x' || p[1]=='X'))
        { p+=2; if (!isxdigit((unsigned char)*p)) return (mcs251_call_merge_op){0,0,0};
          while (isxdigit((unsigned char)*p)) ++p; }
      else { if (!isdigit((unsigned char)*p)) return (mcs251_call_merge_op){0,0,0};
        while (isdigit((unsigned char)*p)) ++p; }
    }
  else
    {
      if (!mcs251CallMergeReg(&p,&other,&width) || width!=op.width) return (mcs251_call_merge_op){0,0,0};
      op.mask|=other;
    }
  mcs251CallMergeSpace(&p); if (!*p || *p==';') op.kind=M251_CM_MOVE;
  return op;
}

/* 返回要删除的节点索引。保持保存字节在栈上，跨过纯寄存器准备段。
 * 所有 POP/PUSH 必须完全反序配对；整个窗口中不得访问这些物理字节。
 * 最后 CALL 由桥接层验证为普通直接调用，其参数不得读取保存集合。 */
static unsigned mcs251CallMergeFind (const mcs251_call_merge_op *ops, unsigned count, unsigned *remove)
{
  unsigned n=0, i=0, mask=0, pushes=0, phase=0;
  while (i<count && ops[i].kind==M251_CM_POP)
    {
      if (n==16 || !ops[i].mask || (mask&ops[i].mask)) return 0;
      mask|=ops[i].mask; remove[n++]=i++;
    }
  if (!n) return 0;
  for (;i<count;++i)
    {
      if (ops[i].kind==M251_CM_MOVE)
        { if (ops[i].mask&mask) return 0; continue; }
      if (ops[i].kind==M251_CM_PUSH)
        {
          if (pushes==n || ops[i].mask!=ops[n-1-pushes].mask || ops[i].width!=ops[n-1-pushes].width) return 0;
          phase=1; remove[n+pushes++]=i; continue;
        }
      if (ops[i].kind==M251_CM_CALL && phase && pushes==n && !(ops[i].mask&mask)) return n*2;
      return 0;
    }
  return 0;
}
#endif
