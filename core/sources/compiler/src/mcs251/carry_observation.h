/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCS251_CARRY_OBSERVATION_H
#define MCS251_CARRY_OBSERVATION_H

static bool
mcs251_carry_operand (const operand *op)
{
  if (!IS_TRUE_SYMOP (op))
    return false;
  sym_link *spec = OP_SYMBOL_CONST (op)->etype;
  return IS_BIT (spec) && SPEC_ABSA (spec) && SPEC_ADDR (spec) == 0xd7;
}

#endif
