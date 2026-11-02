/* Adjacent XDATA address/access fusion. GPL-2.0-or-later. */

/* 只消除立即消费的地址临时值，不缓存内存内容，不跨越其他 iCode。
 * 复用原远访存函数，保留字节顺序、宽访问资格和最终 A/DPX 状态。
 * CODE 路径已有的 WR18 加法和高字节进位覆盖 64 KiB 与 24 位边界。 */
static bool
mcs251GenIndexedFarAccess (iCode *ic)
{
  iCode *access = ic->next;
  operand *base = IC_LEFT (ic), *index = IC_RIGHT (ic);
  operand *pointer = IC_RESULT (ic), *value;
  sym_link *ptype, *itype;
  symbol *vsym;
  const char *indexRegister = NULL;
  bool store, accepted = FALSE;
  int indexSize, valueSize;

  if (!_G.currentFunc || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) || FUNC_REGBANK (_G.currentFunc->type) ||
      !access || access->generated || (!POINTER_GET (access) && !POINTER_SET (access)))
    return FALSE;
  store = POINTER_SET (access);
  if (!IS_ITEMP (pointer) ||
      !isOperandEqual (pointer, store ? IC_RESULT (access) : IC_LEFT (access)) ||
      bitVectnBitsOn (OP_DEFS (pointer)) != 1 || !bitVectBitValue (OP_DEFS (pointer), ic->key) ||
      bitVectnBitsOn (OP_USES (pointer)) != 1 || !bitVectBitValue (OP_USES (pointer), access->key) ||
      OP_SYMBOL (pointer)->isspilt || OP_SYMBOL (pointer)->remat || OP_SYMBOL (pointer)->nRegs != 3 ||
      (!store && IC_RIGHT (access) &&
       (!IS_OP_LITERAL (IC_RIGHT (access)) || operandLitValue (IC_RIGHT (access)) != 0)))
    return FALSE;
  ptype = operandType (pointer);
  if (!IS_PTR (ptype) || DCL_TYPE (ptype) != FPOINTER || getSize (ptype) != 3)
    return FALSE;
  if (!IS_PTR (operandType (base)))
    { operand *swap = base; base = index; index = swap; }
  if (!IS_PTR (operandType (base)) || DCL_TYPE (operandType (base)) != FPOINTER ||
      getSize (operandType (base)) != 3 || !IS_ITEMP (base) || !OP_SYMBOL (base)->remat ||
      !IS_ITEMP (index) || OP_SYMBOL (index)->remat || OP_SYMBOL (index)->ruonly ||
      OP_SYMBOL (index)->isspilt || OP_SYMBOL (index)->accuse || !OP_SYMBOL (index)->nRegs)
    return FALSE;
  itype = operandType (index); indexSize = getSize (itype);
  value = store ? IC_RIGHT (access) : IC_RESULT (access);
  valueSize = getSize (operandType (value));
  if (!IS_INTEGRAL (itype) || IS_BITINT (itype) || !SPEC_USIGN (getSpec (itype)) ||
      (indexSize != 1 && indexSize != 2) ||
      !IS_INTEGRAL (operandType (value)) || IS_BITINT (operandType (value)) ||
      IS_BITVAR (operandType (value)) || IS_BITFIELD (getSpec (operandType (value))) ||
      (valueSize != 1 && valueSize != 2 && valueSize != 4) ||
      IS_BITFIELD (getSpec (ptype->next)) || IS_BITVAR (ptype->next) ||
      IS_AGGREGATE (ptype->next) || getSize (ptype->next) != valueSize ||
      isVolatile (ptype->next) || isVolatile (operandType (base)->next) ||
      isAtomic (ptype->next) || isAtomic (operandType (base)->next) ||
      isOperandVolatileOrAtomic (base, TRUE) || isOperandVolatileOrAtomic (index, TRUE) ||
      isOperandVolatileOrAtomic (pointer, TRUE) || isAtomic (operandType (value)) ||
      hasInc (pointer, access, valueSize) || (!store && ifxForOp (value, access)))
    return FALSE;

  /* aopOp 可能发射寻址/保存指令，故先用符号约束排除远内存与间接 spill。
   * 单字节 SFR 源在地址求值后由原 store 路径读取，保持 volatile 读取一次。
   * 其余真符号、位变量、A/B 临时值和远端 spill 保留原回退路径。 */
  if (store && IS_OP_LITERAL (value))
    { /* Pure literal: aopOp has no addressing side effect. */ }
  else if (store && IS_TRUE_SYMOP (value) && valueSize == 1 &&
           SPEC_SCLS (OP_SYM_ETYPE (value)) == S_SFR)
    { /* Read order and width are retained by genFarPointerSet. */ }
  else
    {
      if (!IS_ITEMP (value) || isOperandVolatileOrAtomic (value, TRUE)) return FALSE;
      vsym = OP_SYMBOL (value);
      if (vsym->remat || vsym->accuse || vsym->ruonly ||
          vsym->regType == REG_CND || vsym->regType == REG_BIT) return FALSE;
      if (vsym->isspilt || !vsym->nRegs)
        {
          symbol *slot = SPIL_LOC (value);
          if (!slot || !slot->onStack || !SPEC_OCLS (slot->etype) || SPEC_OCLS (slot->etype)->paged)
            return FALSE;
        }
      else
        {
          if (vsym->nRegs != valueSize) return FALSE;
          for (int i = 0; i < valueSize; ++i)
            if (!vsym->regs[i] || vsym->regs[i]->type == REG_BIT ||
                vsym->regs[i]->offset < 0 || vsym->regs[i]->offset > 15 ||
                vsym->regs[i]->rIdx == A_IDX || vsym->regs[i]->rIdx == B_IDX)
              return FALSE;
        }
    }
  for (int i = 0; i < indexSize; ++i)
    if (!OP_SYMBOL (index)->regs[i] || OP_SYMBOL (index)->regs[i]->type == REG_BIT ||
        OP_SYMBOL (index)->regs[i]->offset < 0 || OP_SYMBOL (index)->regs[i]->offset > 15 ||
        OP_SYMBOL (index)->regs[i]->rIdx == A_IDX || OP_SYMBOL (index)->regs[i]->rIdx == B_IDX)
      return FALSE;

  aopOp (base, ic, FALSE);
  aopOp (index, ic, FALSE);
  aopOp (value, access, FALSE);
  if (AOP_TYPE (base) != AOP_IMMD || AOP_SIZE (base) != 3 ||
      AOP (base)->aopu.aop_immd.from_cast_remat || AOP (base)->aopu.aop_immd.mcs251_pdata_page ||
      !AOP (base)->aopu.aop_immd.aop_immd1 || !AOP (base)->aopu.aop_immd.aop_immd1[0] ||
      AOP_TYPE (index) != AOP_REG || AOP_SIZE (index) != indexSize ||
      AOP_SIZE (value) != valueSize ||
      (AOP_TYPE (value) != AOP_REG && AOP_TYPE (value) != AOP_MCS251_STK &&
       !(store && (AOP_TYPE (value) == AOP_LIT || AOP_TYPE (value) == AOP_SFR))))
    goto release;
  indexRegister = indexSize == 1 ? AOP (index)->aopu.aop_reg[0]->name :
                  mcs251WordForNativePair (AOP (index), 0);
  if (!indexRegister) goto release;

  _G.dptrCache.valid = FALSE;
  emitcode (";", "mcs251 indexed XDATA %s address %d-bit", store ? "store" : "load", indexSize * 8);
  emitcode (indexSize == 1 ? "movz" : "mov", "wr18,%s", indexRegister);
  emitcode ("add", "wr18,#%s", AOP (base)->aopu.aop_immd.aop_immd1);
  emitcode ("clr", "a");
  emitcode ("addc", "a,%s", opGet (base, 2, FALSE, FALSE));
  emitcode ("movz", "wr16,a");
  emitcode ("mov", "dpx,dr16");
  genLine.lineElement.ic = access;
  if (store) genFarPointerSet (value, pointer, access, NULL, TRUE);
  else genFarPointerGet (pointer, value, access, NULL, NULL, TRUE);
  genLine.lineElement.ic = ic;
  access->generated = 1;
  accepted = TRUE;
release:
  freeAsmop (value, NULL, access, TRUE);
  freeAsmop (index, NULL, ic, TRUE);
  freeAsmop (base, NULL, ic, TRUE);
  return accepted;
}
