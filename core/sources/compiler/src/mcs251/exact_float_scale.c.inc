/* Integer -> binary32 scaling -> integer, without an intermediate rounding.
 * For unsigned x, x * numerator < 2^24 is exactly representable in binary32.
 * Multiplication by 2^-shift is exact in the range admitted here, and the
 * final nonnegative float-to-integer conversion is a logical right shift.
 * Keep the input conversion at its original position, including volatile reads.
 */
static void
mcs251OptimizeExactFloatScale (eBBlock **ebbs, int count)
{
  if (!TARGET_IS_MCS251)
    return;

  for (int i = 0; i < count; ++i)
    for (iCode *cast = ebbs[i]->sch; cast; cast = cast->next)
      {
        iCode *scale, *finish, *shr;
        operand *constant;
        sym_link *input, *output, *work;
        unsigned int inputBits, outputBits, shift = 0;
        unsigned long numerator, maximum, product;
        unsigned long long outputMaximum;
        double factor;

        if (cast->op != CAST || !IS_ITEMP (IC_RESULT (cast)) ||
            !IS_FLOAT (operandType (IC_RESULT (cast))) ||
            getSize (operandType (IC_RESULT (cast))) != 4)
          continue;
        input = operandType (IC_RIGHT (cast));
        if (!IS_INTEGRAL (input) || !IS_UNSIGNED (input) || IS_BOOLEAN (input))
          continue;
        inputBits = bitsForType (input);
        if (!inputBits || inputBits > 16)
          continue;

        scale = cast->next;
        if (!scale || (scale->op != '*' && scale->op != '/') ||
            !IS_ITEMP (IC_RESULT (scale)) ||
            !IS_FLOAT (operandType (IC_RESULT (scale))) ||
            getSize (operandType (IC_RESULT (scale))) != 4 ||
            bitVectnBitsOn (OP_USES (IC_RESULT (cast))) != 1 ||
            bitVectnBitsOn (OP_DEFS (IC_RESULT (cast))) != 1)
          continue;
        if (isOperandEqual (IC_LEFT (scale), IC_RESULT (cast)))
          constant = IC_RIGHT (scale);
        else if (scale->op == '*' && isOperandEqual (IC_RIGHT (scale), IC_RESULT (cast)))
          constant = IC_LEFT (scale);
        else
          continue;
        if (!IS_OP_LITERAL (constant) || !IS_FLOAT (operandType (constant)))
          continue;
        finish = scale->next;
        if (!finish || finish->op != CAST ||
            !isOperandEqual (IC_RIGHT (finish), IC_RESULT (scale)) ||
            bitVectnBitsOn (OP_USES (IC_RESULT (scale))) != 1 ||
            bitVectnBitsOn (OP_DEFS (IC_RESULT (scale))) != 1)
          continue;
        output = operandType (IC_RESULT (finish));
        if (!IS_INTEGRAL (output) || IS_BOOLEAN (output))
          continue;
        outputBits = bitsForType (output);
        if (!outputBits || outputBits > 32)
          continue;

        factor = operandLitValue (constant);
        if (!(factor > 0) || !isfinite (factor))
          continue;
        if (scale->op == '/')
          {
            int exponent;
            /* Only division by powers of two has the same exact scaling. */
            if (frexp (factor, &exponent) != 0.5)
              continue;
            factor = 1.0 / factor;
          }
        while (shift < 31 && factor != floor (factor))
          factor *= 2, ++shift;
        maximum = (1UL << inputBits) - 1;
        if (factor < 1 || factor != floor (factor) || factor > 0xffffffUL / maximum)
          continue;
        numerator = (unsigned long)factor;
        product = maximum * numerator;
        outputMaximum = (1ULL << (outputBits - !IS_UNSIGNED (output))) - 1;
        if ((product >> shift) > outputMaximum)
          continue;

        work = product <= 0xffffUL && shift < 16 ? UINTTYPE : ULONGTYPE;
        IC_LEFT (cast) = operandFromLink (work);
        OP_SYMBOL (IC_RESULT (cast))->type = copyLinkChain (work);
        OP_SYMBOL (IC_RESULT (cast))->etype = getSpec (OP_SYMBOL (IC_RESULT (cast))->type);
        scale->op = '*';
        IC_LEFT (scale) = IC_RESULT (cast);
        IC_RIGHT (scale) = operandFromValue (valCastLiteral (work, numerator, numerator), false);
        OP_SYMBOL (IC_RESULT (scale))->type = copyLinkChain (work);
        OP_SYMBOL (IC_RESULT (scale))->etype = getSpec (OP_SYMBOL (IC_RESULT (scale))->type);
        if (!shift)
          continue;
        shr = newiCode (RIGHT_OP, IC_RESULT (scale), operandFromLit (shift));
        IC_RESULT (shr) = newiTempOperand (work, 0);
        shr->filename = finish->filename;
        shr->lineno = finish->lineno;
        hTabAddItem (&iCodehTab, shr->key, shr);
        bitVectUnSetBit (OP_USES (IC_RESULT (scale)), finish->key);
        OP_USES (IC_RESULT (scale)) = bitVectSetBit (OP_USES (IC_RESULT (scale)), shr->key);
        OP_DEFS (IC_RESULT (shr)) = bitVectSetBit (OP_DEFS (IC_RESULT (shr)), shr->key);
        OP_USES (IC_RESULT (shr)) = bitVectSetBit (OP_USES (IC_RESULT (shr)), finish->key);
        IC_RIGHT (finish) = IC_RESULT (shr);
        addiCodeToeBBlock (ebbs[i], shr, finish);
      }
}
