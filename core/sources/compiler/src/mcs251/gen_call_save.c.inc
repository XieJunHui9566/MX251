/* Shared caller-save/restore plans. GPL-2.0-or-later. */
#include "call_save_plan.h"

static bool
mcs251EmitCallSavePlan (bitVect *rsave, bool restore)
{
  mcs251_call_save_plan plan;
  int registers[16];
  unsigned mask = 0;
  bool bits = FALSE;
  if (!_G.currentFunc || options.useXstack ||
      IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) ||
      FUNC_REGBANK (_G.currentFunc->type)) return FALSE;
  for (unsigned i = 0; i < 16; ++i) registers[i] = -1;
  for (int i = 0; i < mcs251_nRegs; ++i)
    if (bitVectBitValue (rsave, i))
      {
        const reg_info *reg = REG_WITH_INDEX (i);
        if (reg->type == REG_BIT) continue;
        if ((reg->type != REG_GPR && reg->type != REG_PTR) ||
            reg->offset < 0 || reg->offset > 15 ||
            registers[reg->offset] != -1) return FALSE;
        registers[reg->offset] = i;
        mask |= 1u << reg->offset;
      }
  if (!mcs251BuildCallSavePlan (mask, &plan) || !plan.wide) return FALSE;
  emitcode (";", "mcs251 call %s plan %u bytes / %u groups",
            restore ? "restore" : "save", plan.bytes, plan.count);
  /* 位寄存器仍按旧规则合并到一个保存字节；先保存 GPR 后保存 bits，
   * 恢复时反序。_G.stack.pushed 始终按字节计数，保持参数偏移不变。 */
  if (restore)
    for (int i = mcs251_nRegs - 1; i >= 0; --i)
      if (bitVectBitValue (rsave, i) && REG_WITH_INDEX (i)->type == REG_BIT)
        bits = popReg (i, bits);
  for (unsigned i = 0; i < plan.count; ++i)
    {
      mcs251_call_save_step step = plan.steps[restore ? plan.count - 1 - i : i];
      if (step.width == 1)
        {
          if (restore) emitpop (REG_WITH_INDEX (registers[step.first])->dname);
          else emitpush (REG_WITH_INDEX (registers[step.first])->dname);
        }
      else
        {
          emitcode (restore ? "pop" : "push", "%s%u", step.width == 4 ? "dr" : "wr", step.first);
          _G.stack.pushed += restore ? -(int)step.width : (int)step.width;
          wassertl (_G.stack.pushed >= 0, "call-save stack underflow");
        }
    }
  if (!restore)
    for (int i = 0; i < mcs251_nRegs; ++i)
      if (bitVectBitValue (rsave, i) && REG_WITH_INDEX (i)->type == REG_BIT)
        bits = pushReg (i, bits);
  return TRUE;
}
