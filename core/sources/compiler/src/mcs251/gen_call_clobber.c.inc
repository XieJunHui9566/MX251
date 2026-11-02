/* SPDX-License-Identifier: GPL-2.0-or-later
 * 只移除已由同次完整构建证明被调函数不写的 bank-0 R2..R7。
 * R0/R1 保留供参数取值的间接寻址临时用途；ABI4 参数搬运、特殊
 * 入口、间接调用与未知契约保持原路径。保存/恢复使用同一判定和账本。 */
extern bool mcs251CallClobberWrites (symbol *callee, unsigned long *writes);
static void
mcs251TrimKnownCallRegisters (iCode *ic, bitVect *rsave, bool restore)
{
  unsigned long writes;
  unsigned removed = 0;
  symbol *callee;
  sym_link *type;
  if (!_G.currentFunc || options.useXstack || !options.omitFramePtr ||
      FUNC_SDCCCALL (_G.currentFunc->type) != 2 || FUNC_REGBANK (_G.currentFunc->type) ||
      IFFUNC_ISISR (_G.currentFunc->type) || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) || IFFUNC_ISBANKEDCALL (_G.currentFunc->type) ||
      !ic || ic->op != CALL || !IS_SYMOP (IC_LEFT (ic))) return;
  callee = OP_SYMBOL (IC_LEFT (ic)); type = callee->type;
  if (!IS_FUNC (type) || FUNC_SDCCCALL (type) != 2 || FUNC_REGBANK (type) ||
      IFFUNC_ISISR (type) || IFFUNC_ISNAKED (type) || IFFUNC_CALLEESAVES (type) ||
      IFFUNC_ISBANKEDCALL (type) || !mcs251CallClobberWrites (callee, &writes)) return;
  for (int i=0; i<mcs251_nRegs; ++i)
    if (bitVectBitValue (rsave, i))
      {
        const reg_info *reg = REG_WITH_INDEX (i);
        if ((reg->type == REG_GPR || reg->type == REG_PTR) && reg->offset >= 2 && reg->offset < 8 &&
            !(writes & (1UL << reg->offset)))
          {
            bitVectUnSetBit (rsave, i);
            removed |= 1u << reg->offset;
          }
      }
  if (removed)
    emitcode (";", "mx251 call-clobber %s callee=%s removed=%02x writes=%08lx",
              restore ? "restore" : "save", callee->rname, removed, writes);
}
