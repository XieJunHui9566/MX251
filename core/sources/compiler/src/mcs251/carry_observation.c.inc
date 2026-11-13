/* SPDX-License-Identifier: GPL-2.0-or-later
 * Give an immediately observed shift carry an explicit iCode value before
 * generic dead-code elimination. This is the MCS251 source-level CY extension:
 * the outgoing bit belongs to the shift's effective width, including the
 * byte-width compound shifts selected by the front end.
 *
 * Capture from the already sampled operand, then publish CY immediately before
 * its original volatile read. Ordinary optimization may change how the shift is
 * implemented; it can no longer discard the data on which the observed bit
 * depends. No other target or unobserved shift is changed.
 */
#include "carry_observation.h"

static void
mcs251_carry_insert_before (iCode *at, iCode *added)
{
  added->filename = at->filename;
  added->lineno = at->lineno;
  added->level = at->level;
  added->block = at->block;
  added->seqPoint = at->seqPoint;
  added->prev = at->prev;
  added->next = at;
  at->prev->next = added;
  at->prev = added;
}

static void
mcs251_observed_shift_carry (iCode *head)
{
  for (iCode *shift = head; shift; shift = shift->next)
    {
      if ((shift->op != LEFT_OP && shift->op != RIGHT_OP) ||
          !IS_OP_LITERAL (IC_RIGHT (shift)) || operandLitValue (IC_RIGHT (shift)) != 1 ||
          !IS_INTEGRAL (operandType (IC_LEFT (shift))) || IS_BIT (operandType (IC_LEFT (shift))) ||
          IS_BITINT (operandType (IC_LEFT (shift))))
        continue;
      int width = getSize (operandType (IC_LEFT (shift)));
      if ((width != 1 && width != 2 && width != 4) ||
          getSize (operandType (IC_RESULT (shift))) != width)
        continue;

      iCode *read = shift->next;
      /* The compound assignment writes its result before the next statement.
       * Do not cross unrelated operations, labels, calls, or inline assembly. */
      if (read && read->op == '=' && isOperandEqual (IC_RIGHT (read), IC_RESULT (shift)) &&
          IS_SYMOP (IC_RESULT (read)) && !POINTER_SET (read) &&
          getSize (operandType (IC_RESULT (read))) == width)
        read = read->next;
      operand *carry = !read ? NULL : mcs251_carry_operand (IC_RIGHT (read)) ? IC_RIGHT (read) :
                       mcs251_carry_operand (IC_LEFT (read)) ? IC_LEFT (read) : NULL;
      if (!carry)
        continue;

      iCode *bit = newiCode (GETABIT, operandFromOperand (IC_LEFT (shift)),
                            operandFromLit (shift->op == LEFT_OP ? width * 8 - 1 : 0));
      IC_RESULT (bit) = newiTempOperand (UCHARTYPE, 0);
      mcs251_carry_insert_before (shift, bit);
      iCode *publish = newiCode ('=', NULL, operandFromOperand (IC_RESULT (bit)));
      IC_RESULT (publish) = operandFromOperand (carry);
      mcs251_carry_insert_before (read, publish);
    }
}
