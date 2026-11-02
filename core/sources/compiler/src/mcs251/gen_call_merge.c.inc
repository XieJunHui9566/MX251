/* Adjacent call-save bridge. GPL-2.0-or-later. */
#include "call_merge.h"

static bool mcs251MergeCallSaves (lineNode *head)
{
  bool changed=FALSE;
  if (!currFunc || options.useXstack || IFFUNC_ISNAKED(currFunc->type) ||
      IFFUNC_CALLEESAVES(currFunc->type) || FUNC_REGBANK(currFunc->type) ||
      IFFUNC_ISISR(currFunc->type)) return FALSE;
  for (lineNode *p=head;p;p=p->next) if (p->isInline) return FALSE;
  for (lineNode *start=head;start;start=start->next)
    {
      mcs251_call_merge_op ops[96]; lineNode *nodes[96]; unsigned count=0, remove[32];
      if (start->isComment || start->isDebug || start->isLabel ||
          mcs251CallMergeParse(start->line).kind!=M251_CM_POP) continue;
      for (lineNode *p=start;p && count<96;p=p->next)
        {
          if (p->isComment || p->isDebug) continue;
          if (p->isLabel || !p->ic ||
              mcs251TransferObservable(IC_LEFT(p->ic)) ||
              mcs251TransferObservable(IC_RIGHT(p->ic)) ||
              mcs251TransferObservable(IC_RESULT(p->ic))) break;
          mcs251_call_merge_op op=mcs251CallMergeParse(p->line);
          /* 只接纳调用生成器的私有保存，拒绝参数、局部变量和手工栈操作。 */
          if ((op.kind==M251_CM_POP || op.kind==M251_CM_PUSH) && p->ic->op!=CALL) break;
          if (!strncmp(p->line,"ecall\t",6) && p->ic->op==CALL && IS_SYMOP(IC_LEFT(p->ic)))
            {
              sym_link *ftype=operandType(IC_LEFT(p->ic));
              if (!IS_FUNC(ftype) || IFFUNC_CALLEESAVES(ftype) || IFFUNC_ISNAKED(ftype) ||
                  IFFUNC_ISBANKEDCALL(ftype) || FUNC_REGBANK(ftype)) break;
              op=(mcs251_call_merge_op){M251_CM_CALL,0,0};
              if(mcs251InternalArgument(OP_SYMBOL(IC_LEFT(p->ic))))op.mask|=0xf0;
              for (unsigned r=0;r<16;++r)
                { char name[8]; SNPRINTF(name,sizeof name,"r%u",r);
                  if (mcs251IsParmInCall(ftype,name)) op.mask|=1u<<r; }
            }
          if (!op.kind) break;
          ops[count]=op; nodes[count++]=p;
          if (op.kind==M251_CM_CALL) break;
        }
      unsigned n=mcs251CallMergeFind(ops,count,remove);
      for (unsigned i=0;i<n;++i)
        {
          lineNode *p=nodes[remove[i]];
          p->line=Safe_strdup("; mcs251 adjacent call save coalesced");
          p->isComment=1; p->aln=NULL; changed=TRUE;
        }
    }
  return changed;
}
