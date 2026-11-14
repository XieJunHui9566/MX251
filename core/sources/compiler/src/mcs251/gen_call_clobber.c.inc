/* SPDX-License-Identifier: GPL-2.0-or-later
 * 只移除已由同次完整构建证明被调函数不写的 bank-0 R2..R7。
 * R0/R1 保留供参数取值的间接寻址临时用途。参数槽与被调函数的写入
 * 共同决定保存集合；保存/恢复使用同一原型，不依赖已消费的 SEND 集合。 */
extern bool mcs251CallClobberWrites (symbol *callee, unsigned long *writes);

static unsigned long
mcs251CallArgumentWrites (symbol *callee)
{
  sym_link *type = callee->type;
  unsigned long writes = 0;
  int index = 0;
  for (value *arg = FUNC_ARGS (type); arg; arg = arg->next)
    {
      asmop *registers;
      ++index;
      if (IS_VOID (arg->type) || IS_BIT (arg->type)) continue;
      registers = index == 1 && mcs251InternalArgument (callee) ?
        nativeAbiAop (4) : aopArg (type, index);
      if (!registers) continue;
      for (int byte = 0; byte < registers->size; ++byte)
        {
          const reg_info *reg = registers->aopu.aop_reg[byte];
          if ((reg->type == REG_GPR || reg->type == REG_PTR) &&
              reg->offset >= 0 && reg->offset < 32)
            writes |= 1UL << reg->offset;
        }
    }
  return writes;
}

static void
mcs251TrimKnownCallRegisters (iCode *ic, bitVect *rsave, bool restore)
{
  unsigned long writes, arguments;
  unsigned removed = 0;
  symbol *callee;
  sym_link *type;
  if (!_G.currentFunc || options.useXstack || !options.omitFramePtr ||
      (FUNC_SDCCCALL (_G.currentFunc->type) != 2 && FUNC_SDCCCALL (_G.currentFunc->type) != 4) ||
      FUNC_REGBANK (_G.currentFunc->type) ||
      IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) || IFFUNC_ISBANKEDCALL (_G.currentFunc->type) ||
      !ic || ic->op != CALL || !IS_SYMOP (IC_LEFT (ic))) return;
  callee = OP_SYMBOL (IC_LEFT (ic)); type = callee->type;
  /* A bank0 ISR has the same call-site register lifetimes as an ordinary
     caller. Its asynchronous entry/exit context is saved independently. */
  if (!IS_FUNC (type) || (FUNC_SDCCCALL (type) != 2 && FUNC_SDCCCALL (type) != 4) || FUNC_REGBANK (type) ||
      IFFUNC_ISISR (type) || IFFUNC_ISNAKED (type) || IFFUNC_CALLEESAVES (type) ||
      IFFUNC_ISBANKEDCALL (type) || !mcs251CallClobberWrites (callee, &writes)) return;
  /* SEND writes happen after saveRegisters, even when the callee leaves its
     argument registers untouched. Native parallel moves write only their
     destination slots; memory sources additionally use A/B, DPX and R0/R1. */
  arguments = mcs251CallArgumentWrites (callee);
  for (int i=0; i<mcs251_nRegs; ++i)
    if (bitVectBitValue (rsave, i))
      {
        const reg_info *reg = REG_WITH_INDEX (i);
        if ((reg->type == REG_GPR || reg->type == REG_PTR) && reg->offset >= 2 && reg->offset < 8 &&
            !((writes | arguments) & (1UL << reg->offset)))
          {
            bitVectUnSetBit (rsave, i);
            removed |= 1u << reg->offset;
          }
      }
  if (removed)
    emitcode (";", "mx251 call-clobber %s callee=%s removed=%02x writes=%08lx arguments=%08lx",
              restore ? "restore" : "save", callee->rname, removed, writes, arguments);
}
