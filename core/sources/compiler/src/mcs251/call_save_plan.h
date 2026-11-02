/* MCS251 private caller-save layout planning. GPL-2.0-or-later. */
#ifndef MCS251_CALL_SAVE_PLAN_H
#define MCS251_CALL_SAVE_PLAN_H
#include "register_facts.h"

typedef struct { unsigned first, width; } mcs251_call_save_step;
typedef struct
{
  mcs251_call_save_step steps[14];
  unsigned count, bytes, wide;
} mcs251_call_save_plan;

/* 仅描述调用者私有保存区；参数和返回值布局不变。WR/DR 的硬件入栈
 * 字节序与原逐字节顺序不同，恢复必须严格倒序使用同一计划。
 * A/B 别名不进入此模型；寄存器组及特殊函数由调用点先行排除。 */
static int
mcs251BuildCallSavePlan (unsigned mask, mcs251_call_save_plan *plan)
{
  plan->count = plan->bytes = plan->wide = 0;
  if (mask & ~(UINT32_C(0xffff) & ~MCS251_AB_BYTES)) return 0;
  for (unsigned bank = 0; bank < 2; ++bank)
    for (int top = bank ? 15 : 7; top >= (bank ? 8 : 0); )
      {
        unsigned width = 1;
        if (!(mask & (1u << top))) { --top; continue; }
        if (top % 4 == 3 && (mask & mcs251RegisterBytes (top - 3, 4)) == mcs251RegisterBytes (top - 3, 4)) width = 4;
        else if (top % 2 == 1 && (mask & mcs251RegisterBytes (top - 1, 2)) == mcs251RegisterBytes (top - 1, 2)) width = 2;
        plan->steps[plan->count].first = top - width + 1;
        plan->steps[plan->count++].width = width;
        plan->bytes += width;
        plan->wide += width > 1;
        top -= width;
      }
  return 1;
}
#endif
