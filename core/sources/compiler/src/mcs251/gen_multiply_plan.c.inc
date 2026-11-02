/* MCS251 乘法计划；GPL-2.0-or-later。
 * 复用标量位置和搬运描述，临时值只活到当前 iCode 结束，不改变 ABI2。 */
static bool
mcs251MultiplyPreservesByte (iCode *ic, operand *result, int offset)
{
  bool preserve = !_G.currentFunc || IFFUNC_ISISR (_G.currentFunc->type) ||
                  IFFUNC_CALLEESAVES (_G.currentFunc->type) ||
                  IFFUNC_ISNAKED (_G.currentFunc->type) ||
                  mcs251RegisterSurvives (ic, mcs251_regIdxForNumber (offset));
  if (AOP_TYPE (result) == AOP_REG)
    for (int i = 0; i < AOP_SIZE (result); ++i)
      if (AOP (result)->aopu.aop_reg[i]->offset == offset)
        return FALSE;
  return preserve;
}

/* 旧路径的指令数估计，包括活跃寄存器保存、操作数快照和结果搬运。
 * 字节快照按每个读写至少一条指令计，是保守下界；不把它当成周期。
 * 同一保存判据同时供旧路径发射使用，避免成本模型漏算实际保存。 */
static unsigned
mcs251LegacyMultiplyInstructions (iCode *ic, operand *left, operand *right,
                                  operand *result)
{
  static const int scratch[] = {8, 9, 12, 13, 14, 15};
  const char *l = mcs251WordForNativePair (AOP (left), 0);
  const char *r = mcs251WordForNativePair (AOP (right), 0);
  const char *d = mcs251DwordForNativeTuple (AOP (result));
  const char *low = mcs251WordForNativePair (AOP (result), 0);
  const char *high = mcs251WordForNativePair (AOP (result), 2);
  unsigned count = 1;
  for (int i = 0; i < 6; ++i)
    if (mcs251MultiplyPreservesByte (ic, result, scratch[i])) count += 2;
  if (l && r && (strcmp (l, "wr8") || strcmp (r, "wr12")))
    {
      if (!strcmp (r, "wr12")) count += 1 + (strcmp (l, "wr12") != 0);
      else count += (strcmp (l, "wr12") != 0) + (strcmp (r, "wr8") != 0);
    }
  else count += 12;
  if (d) count += strcmp (d, "dr12") != 0;
  else if (low && high && strcmp (low, high) &&
           (strcmp (low, "wr12") || strcmp (high, "wr14")))
    {
      if (!strcmp (high, "wr14")) count += 1 + (strcmp (low, "wr14") != 0);
      else count += (strcmp (high, "wr12") != 0) + (strcmp (low, "wr14") != 0);
    }
  else count += 12;
  return count;
}

static bool
mcs251GenScalarMultiplyPlan (iCode *ic, operand *left, operand *right,
                             operand *result)
{
  mcs251_scalar_location l, r, d;
  unsigned instructions, bytes, legacy;
  if (!_G.currentFunc || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) ||
      !mcs251PlanScalarLocation (left, &l) ||
      !mcs251PlanScalarLocation (right, &r) ||
      !mcs251PlanScalarLocation (result, &d) ||
      l.size != 2 || r.size != 2 || d.size != 4)
    return FALSE;
  instructions = l.instructions + r.instructions + 1 + d.instructions;
  bytes = l.bytes + r.bytes + 2 + d.bytes;
  legacy = mcs251LegacyMultiplyInstructions (ic, left, right, result);
  /* 尺寸优先只在新编码小于旧指令数下界时选择，宁可少优化。
   * 普通 ISR 已保存 DR16/DR20，故不需要按旧活跃值再保存 R8..R15。 */
  if (instructions >= legacy || (optimize.codeSize && bytes >= legacy))
    return FALSE;
  emitcode (";", "mcs251 scalar multiply plan 16x16 (%u vs %u instructions)",
            instructions, legacy);
  mcs251EmitScalarTransfer (&l, 16, FALSE);
  mcs251EmitScalarTransfer (&r, 20, FALSE);
  emitcode ("mul", "wr16,wr20");
  mcs251EmitScalarTransfer (&d, 16, TRUE);
  return TRUE;
}
