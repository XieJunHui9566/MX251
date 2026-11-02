/* 无符号 16 位模乘候选；GPL-2.0-or-later。
 * 输入只读取一次，完整乘积的低字位于 WR18，不是 WR16。
 * 对不规则布局保留逐字节快照，不放宽 volatile 或特殊入口的规则。 */
static bool
mcs251GenLowWordMultiply (iCode *ic, operand *left, operand *right, operand *result)
{
  mcs251_scalar_location l, r, d;
  bool llit = AOP_TYPE (left) == AOP_LIT;
  bool rlit = AOP_TYPE (right) == AOP_LIT;
  bool fast;
  const char *resultWord;
  static const char *names[] = {"r8", "r9", "r12", "r13", "r14", "r15"};
  static const int numbers[] = {8, 9, 12, 13, 14, 15};
  bool preserve[6], rightFirst;
  operand *one, *two;
  int i, first, second;

  if (!getenv ("MX251_LOW_WORD_MUL") || AOP_SIZE (left) != 2 ||
      AOP_SIZE (right) != 2 || AOP_SIZE (result) != 2 ||
      !SPEC_USIGN (getSpec (operandType (left))) ||
      !SPEC_USIGN (getSpec (operandType (right)))) return FALSE;

  fast = _G.currentFunc && !IFFUNC_ISNAKED (_G.currentFunc->type) &&
         !IFFUNC_CALLEESAVES (_G.currentFunc->type) &&
         !FUNC_REGBANK (_G.currentFunc->type) &&
         (llit || mcs251PlanScalarLocation (left, &l)) &&
         (rlit || mcs251PlanScalarLocation (right, &r)) &&
         mcs251PlanScalarLocation (result, &d);
  if (fast)
    {
      emitcode (";", "mcs251 low-word multiply plan");
      if (llit) emitcode ("mov", "wr16,#0x%04x", (unsigned) ullFromVal (AOP (left)->aopu.aop_lit) & 0xffffu);
      else mcs251EmitScalarTransfer (&l, 16, FALSE);
      if (rlit) emitcode ("mov", "wr20,#0x%04x", (unsigned) ullFromVal (AOP (right)->aopu.aop_lit) & 0xffffu);
      else mcs251EmitScalarTransfer (&r, 20, FALSE);
      emitcode ("mul", "wr16,wr20");
      mcs251EmitScalarTransfer (&d, 18, TRUE);
      return TRUE;
    }

  /* 前端已经保留乘法 iCode，因此任何分配结果均必须可发射。
   * 与既有 16x16→32 路径使用同一保存判据；特殊入口全部保存。
   * 含 A 的源先快照，避免另一源的内存读取覆盖它。 */
  emitcode (";", "mcs251 low-word multiply byte fallback");
  for (i = 0; i < 6; ++i)
    {
      preserve[i] = mcs251MultiplyPreservesByte (ic, result, numbers[i]);
      if (preserve[i]) emitpush (names[i]);
    }
  rightFirst = aopInReg (AOP (right), 0, A_IDX) || aopInReg (AOP (right), 1, A_IDX);
  one = rightFirst ? right : left; two = rightFirst ? left : right;
  for (i = 0; i < 2; ++i) emitpush (opGet (one, i, FALSE, TRUE));
  for (i = 0; i < 2; ++i) emitpush (opGet (two, i, FALSE, TRUE));
  /* 乘法可交换；无论快照先后，取独立 WR12 与 WR8，不让源覆盖乘积。 */
  emitpop ("r8"); emitpop ("r9"); emitpop ("r12"); emitpop ("r13");
  emitcode ("mul", "wr12,wr8");
  resultWord = mcs251WordForNativePair (AOP (result), 0);
  if (resultWord && strcmp (resultWord, "wr10"))
    {
      if (strcmp (resultWord, "wr14")) emitcode ("mov", "%s,wr14", resultWord);
    }
  else
    {
      const char *answer[] = {"r15", "r14"};
      first = aopInReg (AOP (result), 0, A_IDX) ? 1 : 0;
      second = 1 - first;
      emitpush (answer[second]); emitpush (answer[first]);
      emitpop ("acc"); opPut (result, "a", first);
      emitpop ("acc"); opPut (result, "a", second);
    }
  for (i = 6; i-- > 0;) if (preserve[i]) emitpop (names[i]);
  return TRUE;
}
