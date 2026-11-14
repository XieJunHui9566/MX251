/* Keep the object and byte displacement of a rematerialized address until
   pointer lowering. The printed immediate alone loses its address space. */
static symbol *
mcs251EdataPointerObject (operand *pointer, long *offset, unsigned depth)
{
  sym_link *type;
  iCode *definition;
  operand *source;

  if (!pointer || !IS_ITEMP (pointer) || depth > 12 ||
      !OP_SYMBOL (pointer)->remat || isOperandVolatileOrAtomic (pointer, TRUE))
    return NULL;
  type = operandType (pointer);
  if (!IS_PTR (type) || getSize (type) != 3 ||
      (DCL_TYPE (type) != FPOINTER && DCL_TYPE (type) != GPOINTER))
    return NULL;
  definition = OP_SYMBOL (pointer)->rematiCode;
  if (!definition || !isOperandEqual (IC_RESULT (definition), pointer))
    return NULL;
  if (definition->op == ADDRESS_OF)
    {
      symbol *object;
      memmap *space;
      int size;
      if (!IS_TRUE_SYMOP (IC_LEFT (definition)) ||
          (IC_RIGHT (definition) && (!IS_OP_LITERAL (IC_RIGHT (definition)) ||
           operandLitValue (IC_RIGHT (definition)) != 0)))
        return NULL;
      object = OP_SYMBOL (IC_LEFT (definition));
      space = SPEC_OCLS (object->etype);
      size = getSize (object->type);
      if (object->onStack || (space != edata && space != e_abs) ||
          size <= 0 || size > 65536 || isAtomic (object->type) ||
          (SPEC_ABSA (object->etype) && (SPEC_ADDR (object->etype) < 0 ||
           SPEC_ADDR (object->etype) > 65536 - size)))
        return NULL;
      return object;
    }
  if (definition->op == CAST)
    source = IC_RIGHT (definition);
  else if (definition->op == '+' || definition->op == '-')
    {
      long displacement;
      if (!IS_OP_LITERAL (IC_RIGHT (definition))) return NULL;
      displacement = (long) operandLitValue (IC_RIGHT (definition));
      if (displacement < -65535 || displacement > 65535) return NULL;
      *offset += definition->op == '+' ? displacement : -displacement;
      source = IC_LEFT (definition);
    }
  else
    return NULL;
  return mcs251EdataPointerObject (source, offset, depth + 1);
}

static bool
mcs251GenEdataPointerAccess (iCode *ic, iCode *pi, iCode *ifx)
{
  bool store = POINTER_SET (ic);
  operand *pointer = store ? IC_RESULT (ic) : IC_LEFT (ic);
  operand *value = store ? IC_RIGHT (ic) : IC_RESULT (ic);
  sym_link *ptype = operandType (pointer), *vtype = operandType (value);
  int size = getSize (vtype), physical;
  long offset = 0;
  symbol *object;

  if (pi || !IS_PTR (ptype) || size < 1 || size > 4 ||
      IS_AGGREGATE (vtype) || IS_AGGREGATE (ptype->next) ||
      IS_BITVAR (vtype) || IS_BITVAR (ptype->next) ||
      IS_BITFIELD (getSpec (vtype)) || IS_BITFIELD (getSpec (ptype->next)) ||
      isAtomic (vtype) || isAtomic (ptype->next) || getSize (ptype->next) != size ||
      (!store && IC_RIGHT (ic) && (!IS_OP_LITERAL (IC_RIGHT (ic)) ||
       operandLitValue (IC_RIGHT (ic)) != 0)))
    return FALSE;
  object = mcs251EdataPointerObject (pointer, &offset, 0);
  if (!object || offset < 0 || offset > getSize (object->type) - size)
    return FALSE;

  if (size > 1) ifx = NULL;
  aopOp (value, ic, !store);
  _G.dptrCache.valid = FALSE;
  emitcode (";", "mcs251 direct EDATA pointer %s object=%s offset=%ld size=%d",
            store ? "store" : "load", object->rname, offset, size);
  /* Pointer transactions advance through physical bytes. Scalar asmops use
     logical least-significant-byte offsets; keep that mapping at the value. */
  for (physical = 0; physical < size; ++physical)
    {
      int logical = mcs251PointerByteOffset (vtype, physical, size);
      if (store)
        {
          MOVA (opGet (value, logical, FALSE, FALSE));
          emitcode ("mov", "e:(%s+%ld),a", object->rname, offset + physical);
        }
      else
        {
          emitcode ("mov", "a,e:(%s+%ld)", object->rname, offset + physical);
          if (!ifx) opPut (value, "a", logical);
        }
    }
  if (ifx && !ifx->generated)
    genIfxJump (ifx, "a", NULL, NULL, value, ic->next);
  freeAsmop (value, NULL, ic, TRUE);
  return TRUE;
}
