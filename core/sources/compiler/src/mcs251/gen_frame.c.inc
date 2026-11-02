/* Frame-location instruction selection. GPL-2.0-or-later.
   Included after the generic scalar transfer helpers. */
static bool
mcs251GenFrameAccess (iCode *ic)
{
  const mcs251_frame_view *v = mcs251FrameView (IC_RESULT (ic));
  bool store = v != NULL;
  operand *value = store ? IC_RIGHT (ic) : IC_RESULT (ic);
  int size = getSize (operandType (value));
  if (!v)
    v = mcs251FrameView (IC_RIGHT (ic));
  if (!v)
    return FALSE;
  aopOp (value, ic, !store);
  /* 值的普通分配发生在降级之后：可能落在寄存器、DATA、XDATA或栈上。
     每字节重新计算栈位置，因此溢出访问和临时压栈不会改变寻址含义。 */
  wassert (AOP_TYPE (value) != AOP_ACC && AOP_TYPE (value) != AOP_CRY &&
           AOP_TYPE (value) != AOP_STR);
  emitcode (";", "mcs251 frame %s %s+%d", store ? "store" : "load", v->object->name, v->byteOffset);
  for (int physical = 0; physical < size;)
    {
      char address[32];
      int logical = size - physical - 1;
      int displacement = stackoffset (v->object) + v->byteOffset + physical;
      const char *word = NULL;
      if (!v->byteAccess && physical + 1 < size && displacement >= -32768 && displacement < 32767)
        word = mcs251WordForNativePair (AOP (value), logical - 1);
      if (word)
        {
          mcs251FormatStackOperand (address, sizeof address, displacement);
          emitcode ("mov", "%s,%s", store ? address : word, store ? word : address);
          physical += 2;
          continue;
        }
      if (store)
        MOVA (opGet (value, logical, FALSE, FALSE));
      displacement = stackoffset (v->object) + v->byteOffset + physical;
      if (displacement >= -32768 && displacement <= 32767)
        {
          mcs251FormatStackOperand (address, sizeof address, displacement);
          emitcode ("mov", store ? "%s,a" : "a,%s", address);
        }
      else
        {
          /* 大栈帧不能截断位移；用完整24位地址，不占长期指针寄存器。 */
          emitcode ("mov", "dpx,spx");
          emitcode (displacement < 0 ? "sub" : "add", "dpx,#%d", abs (displacement));
          emitcode ("mov", store ? "@dpx,a" : "a,@dpx");
        }
      if (!store)
        opPut (value, "a", logical);
      ++physical;
    }
  emitcode (";", "mcs251 end frame access");
  freeAsmop (value, NULL, ic, TRUE);
  return TRUE;
}
