/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keep a single SFR bit in the bit domain when its sole consumer is a
 * branch or a bit assignment. The read stays at the original iCode. */
static bool
mcs251GenSfrBitTest (iCode *ic, operand *source, unsigned bit)
{
  const char *option = getenv ("MX251_SFR_BIT_TESTS");
  if (options.nopeep || (option && !strcmp (option, "0")) ||
      !IS_TRUE_SYMOP (source) || getSize (operandType (source)) != 1 || bit >= 8)
    return false;
  sym_link *spec = getSpec (operandType (source));
  if (SPEC_SCLS (spec) != S_SFR || !SPEC_ABSA (spec) ||
      SPEC_ADDR (spec) < 0x80 || SPEC_ADDR (spec) > 0xff || isAtomic (spec))
    return false;

  operand *result = IC_RESULT (ic), *target = NULL;
  iCode *use = ic->next;
  bool branch = false;
  if (IS_BIT (operandType (result)))
    target = result;
  else
    {
      if (!IS_ITEMP (result) || !use || use->generated ||
          bitVectnBitsOn (OP_DEFS (result)) != 1 ||
          bitVectnBitsOn (OP_USES (result)) != 1 ||
          !bitVectBitValue (OP_USES (result), use->key))
        return false;
      if (use->op == IFX && isOperandEqual (IC_COND (use), result))
        branch = true;
      else if (use->op == CAST && isOperandEqual (IC_RIGHT (use), result) &&
               IS_BIT (operandType (IC_RESULT (use))))
        target = IC_RESULT (use);
      else
        return false;
    }

  char address[16];
  snprintf (address, sizeof address, "0x%02x.%u", (unsigned)SPEC_ADDR (spec), bit);
  emitcode (";", "mx251 direct SFR bit test address=%s consumer=%s",
            address, branch ? "branch" : "bit");
  if (branch)
    genIfxJump (use, address, NULL, NULL, NULL, use);
  else
    {
      aopOp (target, ic, TRUE);
      emitcode ("mov", "c,%s", address);
      outBitC (target);
      freeAsmop (target, NULL, ic, TRUE);
      if (target != result)
        use->generated = 1;
    }
  return true;
}

/* A volatile byte read may have its own snapshot iCode before the mask.
 * Fuse only an adjacent, single-use chain, so the read is neither repeated
 * nor moved past another operation. */
static bool
mcs251GenSfrBitSnapshot (iCode *ic)
{
  operand *temp = IC_RESULT (ic);
  iCode *mask = ic->next;
  if (!IS_ITEMP (temp) || !mask || mask->op != BITWISEAND || mask->generated ||
      bitVectnBitsOn (OP_DEFS (temp)) != 1 ||
      bitVectnBitsOn (OP_USES (temp)) != 1 ||
      !bitVectBitValue (OP_USES (temp), mask->key))
    return false;
  operand *literal = NULL;
  if (isOperandEqual (IC_LEFT (mask), temp)) literal = IC_RIGHT (mask);
  else if (isOperandEqual (IC_RIGHT (mask), temp)) literal = IC_LEFT (mask);
  if (!literal || !IS_OP_LITERAL (literal)) return false;
  int bit = isLiteralBit (ullFromVal (OP_VALUE (literal)));
  if (!bit || !mcs251GenSfrBitTest (mask, IC_RIGHT (ic), bit - 1)) return false;
  mask->generated = 1;
  return true;
}
