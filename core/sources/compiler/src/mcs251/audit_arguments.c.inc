/* Audit-only typed SEND facts. Actual machine entry proof remains mandatory. */
static void
audit_argument_value (const iCode *call, symbol *target, unsigned ordinal)
{
  const iCode *p, *send = NULL;
  value *formal = target && IS_FUNC (target->type) ? FUNC_ARGS (target->type) : NULL;
  for (unsigned k = 1; k < ordinal && formal; ++k) formal = formal->next;
  unsigned walked = 0;
  bool duplicate = false;
  operand *arg = NULL;
  sym_link *type = NULL;
  double bits = -1;
  fprintf (audit_file, "{\"ordinal\":%u", ordinal);
  if (call->op == CALL && formal && ordinal >= 1 && ordinal <= 2 &&
      FUNC_SDCCCALL (target->type) == 4 && IS_REGPARM (formal->etype) &&
      SPEC_ARGREG (formal->etype) == ordinal && IS_CHAR (formal->type) &&
      getSize (formal->type) == 1 && SPEC_USIGN (getSpec (formal->type)) &&
      !isVolatile (formal->type) && !isAtomic (formal->type))
    for (p = call->prev; p && ++walked <= 1024; p = p->prev)
      {
        if (p->eBBlockNum != call->eBBlockNum || p->op == CALL || p->op == PCALL ||
            p->op == INLINEASM || p->op == LABEL || p->op == GOTO || p->op == IFX ||
            p->op == FUNCTION || p->op == RETURN || p->op == JUMPTABLE) break;
        if (p->op == SEND && p->argreg == ordinal)
          { if (send) { duplicate = true; break; } send = p; }
      }
  if (send && !duplicate && walked <= 1024)
    {
      arg = IC_LEFT (send);
      type = arg ? operandType (arg) : NULL;
      if (IS_OP_LITERAL (arg) && type && IS_CHAR (type) && getSize (type) == 1 &&
          SPEC_USIGN (getSpec (type)) && !isVolatile (type) && !isAtomic (type))
        bits = operandLitValue (arg);
    }
  if (bits < 0 || bits > 255 || bits != (unsigned) bits)
    fputs (",\"status\":\"unknown\",\"reason\":\"no-unique-same-block-typed-u8-literal-send\"}", audit_file);
  else
    fprintf (audit_file, ",\"status\":\"typed-literal\",\"type\":\"u8\",\"bits\":%u,"
             "\"send_icode_key\":%d,\"send_argreg\":%u,\"block\":%d,\"walked_nodes\":%u}",
             (unsigned) bits, send->key, ordinal, call->eBBlockNum, walked);
}

static void
audit_actual_arguments (const iCode *call, symbol *target)
{
  fputs (",\"actual_arguments\":[", audit_file);
  for (unsigned ordinal = 1; ordinal <= 2; ++ordinal)
    { if (ordinal > 1) fputc (',', audit_file); audit_argument_value (call, target, ordinal); }
  fputc (']', audit_file);
}
