/* Register transfer cleanup using the shared byte-slice value model.
 * GPL-2.0-or-later. No ABI or allocator changes. */
#include "transfer_values.h"

static bool
mcs251TransferObservable (operand *op)
{
  return op && IS_SYMOP (op) &&
         (isOperandVolatile (op, TRUE) || isAtomic (operandType (op)));
}

static void
mcs251EliminateRedundantTransfers (lineNode *head)
{
  mcs251_transfer_values state;
  unsigned live = MCS251_TRANSFER_ALL_LIVE;
  lineNode *tail = head;
  /* 手写汇编可能含固定 PC 偏移；整函数不改。非零寄存器组的 Rn 与
   * WR/DR 别名不在此版本模型中。保留特殊入口与现有保存约定。 */
  if (!currFunc || IFFUNC_ISNAKED (currFunc->type) ||
      IFFUNC_CALLEESAVES (currFunc->type) || FUNC_REGBANK (currFunc->type)) return;
  for (lineNode *p = head; p; p = p->next) if (p->isInline) return;
  while (tail && tail->next) tail = tail->next;
  for (lineNode *p = tail; p; p = p->prev)
    {
      if (p->isLabel || !p->ic ||
          mcs251TransferObservable (IC_LEFT (p->ic)) ||
          mcs251TransferObservable (IC_RIGHT (p->ic)) ||
          mcs251TransferObservable (IC_RESULT (p->ic)))
        { live = MCS251_TRANSFER_ALL_LIVE; continue; }
      if (p->isComment || p->isDebug) continue;
      if (mcs251TransferDead (&live, p->line))
        {
          p->line = Safe_strdup ("; mcs251 dead register transfer");
          p->isComment = 1; p->aln = NULL;
        }
    }
  mcs251TransferReset (&state);
  for (lineNode *p = head; p; p = p->next)
    {
      if (p->isLabel || !p->ic ||
          mcs251TransferObservable (IC_LEFT (p->ic)) ||
          mcs251TransferObservable (IC_RIGHT (p->ic)) ||
          mcs251TransferObservable (IC_RESULT (p->ic)))
        { mcs251TransferReset (&state); continue; }
      if (p->isComment || p->isDebug) continue;
      if (mcs251TransferApply (&state, p->line))
        {
          /* MOV 不改变 CY/AC/OV/N/Z；本模型也排除了 A/B 别名。删除
           * 已知同值搬运而保留节点，维持调试位置及链表所有权。 */
          p->line = Safe_strdup ("; mcs251 redundant register transfer");
          p->isComment = 1;
          p->aln = NULL;
        }
    }
}
