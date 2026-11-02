/* SPDX-License-Identifier: GPL-2.0-or-later
 * ABI4 参数并行搬运。独立于 ABI2/3；不得逐参数覆盖尚未读取的输入。
 */
static void
nativeParallelMove (asmop **dst, asmop **src, int count)
{
  bool done[2] = {false, false};
  /* 无跨参数依赖时保留 MOV WR/DR；组内依赖交给既有 genMove。 */
  for (int pass = 0; pass < count; ++pass)
    for (int i = 0; i < count; ++i)
      {
        bool safe = !done[i];
        for (int j = 0; j < count && safe; ++j)
          if (i != j && !done[j])
            for (int a = 0; a < dst[i]->size; ++a)
              for (int b = 0; b < src[j]->size; ++b)
                if (dst[i]->aopu.aop_reg[a] == src[j]->aopu.aop_reg[b]) safe = false;
        if (safe) {genMove (dst[i], src[i], true); done[i] = true;}
      }

  /* 两个完整对齐槽互换时，只保存一个槽。严格检查两端完整匹配，
     不把部分重叠、混合宽度或带额外消费者的图误当成二元交换。 */
  if (count == 2 && !done[0] && !done[1] && dst[0]->size == dst[1]->size &&
      src[0]->size == dst[0]->size && src[1]->size == dst[1]->size)
    {
      int width = dst[0]->size;
      const char *d0 = width == 4 ? mcs251DwordForNativeTuple (dst[0]) :
                       width == 2 ? mcs251WordForNativePair (dst[0], 0) : NULL;
      const char *d1 = width == 4 ? mcs251DwordForNativeTuple (dst[1]) :
                       width == 2 ? mcs251WordForNativePair (dst[1], 0) : NULL;
      const char *s0 = width == 4 ? mcs251DwordForNativeTuple (src[0]) :
                       width == 2 ? mcs251WordForNativePair (src[0], 0) : NULL;
      const char *s1 = width == 4 ? mcs251DwordForNativeTuple (src[1]) :
                       width == 2 ? mcs251WordForNativePair (src[1], 0) : NULL;
      if (d0 && d1 && s0 && s1 && !strcmp (d0, s1) && !strcmp (d1, s0) && strcmp (d0, d1))
        {
          emitcode ("push", "%s", s0); _G.stack.pushed += width;
          emitcode ("mov", "%s,%s", d1, s1);
          emitcode ("pop", "%s", d0); _G.stack.pushed -= width;
          return;
        }
    }

  /* 字节依赖图支持重复源和任意环；不采用会破坏第三个消费者的二元交换。 */
  reg_info *to[8], *from[8];
  bool moved[8] = {false};
  int n = 0, pending = 0;
  for (int i = 0; i < count; ++i)
    if (!done[i])
      for (int b = 0; b < dst[i]->size; ++b)
        {
          to[n] = dst[i]->aopu.aop_reg[b]; from[n] = src[i]->aopu.aop_reg[b];
          moved[n] = to[n] == from[n]; pending += !moved[n]; ++n;
        }
  while (pending)
    {
      bool progress = false;
      for (int i = 0; i < n; ++i)
        {
          if (moved[i]) continue;
          bool safe = true;
          for (int j = 0; j < n; ++j)
            if (!moved[j] && i != j && to[i] == from[j]) safe = false;
          if (!safe) continue;
          emitcode ("mov", "%s,%s", to[i]->name, from[i]->name);
          moved[i] = true; --pending; progress = true;
        }
      if (progress) continue;
      for (int i = 0; i < n; ++i) if (!moved[i]) emitpush (from[i]->dname);
      for (int i = n; i-- > 0;) if (!moved[i]) {emitpop (to[i]->dname); moved[i] = true; --pending;}
    }
}

/* 只准入无需读取对象或页寄存器的三字节远地址。PDATA 转换会读取
   P2/MXAX，cast remat 也有额外语义，均保留既有快照路径。原始操作数
   的限定必须单独检查：remat asmop 不保证携带 volatile/atomic 信息。 */
static bool
nativePureFarAddress (operand *op, const asmop *dst)
{
  const asmop *src = AOP (op);
  sym_link *type = operandType (op);
  return src->type == AOP_IMMD && dst->type == AOP_REG &&
         IS_PTR (type) && DCL_TYPE (type) == FPOINTER &&
         getSize (type) == 3 && !IS_FUNC (type->next) &&
         src->size == 3 && dst->size == 3 &&
         !src->aop_is_volatile && !src->aop_is_aggregate &&
         !isOperandVolatile (op, TRUE) && !isAtomic (type) &&
         !src->aopu.aop_immd.from_cast_remat &&
         !src->aopu.aop_immd.mcs251_pdata_page &&
         src->aopu.aop_immd.aop_immd1 && *src->aopu.aop_immd.aop_immd1;
}

static bool
genNativeMultiSend (set *sendSet)
{
  iCode *first = setFirstItem (sendSet), *call;
  if (!first) return false;
  for (call = first->next; call && call->op != CALL && call->op != PCALL; call = call->next);
  if (!call) return false;
  sym_link *ftype = operandType (IC_LEFT (call));
  if (IS_FUNCPTR (ftype)) ftype = ftype->next;
  if (!IS_FUNC (ftype) || FUNC_SDCCCALL (ftype) != 4) return false;
  iCode *items[2]; asmop *dst[2], *src[2]; int n = 0;
  bool allregs = true, simpleSources = true, fixedSources = true;
  for (iCode *s = setFirstItem (sendSet); s; s = setNextItem (sendSet))
    {
      wassert (n < 2);
      items[n] = s; dst[n] = aopArg (ftype, s->argreg);
      aopOp (s->left, s, FALSE); src[n] = s->left->aop;
      allregs &= src[n]->type == AOP_REG;
      simpleSources &= src[n]->type == AOP_REG || src[n]->type == AOP_LIT ||
                      nativePureFarAddress (s->left, dst[n]) ||
                      (src[n]->type == AOP_MCS251_STK &&
                       !src[n]->aop_is_volatile && !src[n]->aop_is_aggregate &&
                       src[n]->size == dst[n]->size &&
                       (src[n]->size == 1 || src[n]->size == 2 || src[n]->size == 4));
      fixedSources &= src[n]->type == AOP_REG || src[n]->type == AOP_LIT ||
                      src[n]->type == AOP_DIR ||
                      (src[n]->type == AOP_DPTR && src[n]->aopu.aop_sym);
      ++n;
    }
  if (allregs)
    {
      nativeParallelMove (dst, src, n);
      for (int i = n; i-- > 0;) freeAsmop (items[i]->left, NULL, items[i], TRUE);
      return true;
    }
  /* 先完成寄存器并行搬运，保护可能来自 A/B 的源值。原生参数槽为
     R4..R7/R12..R15；标量 SPX 读取只用目标槽和 A，不破坏另一参数槽。
     纯远地址只向目标槽写入重定位常量；必须在寄存器源搬完后装载，
     避免地址槽覆盖另一个参数的旧值。其它内存源保留快照。 */
  if (simpleSources)
    {
      asmop *rsrc[2], *rdst[2]; int count = 0;
      for (int i = 0; i < n; ++i)
        if (src[i]->type == AOP_REG) {rsrc[count] = src[i]; rdst[count++] = dst[i];}
      nativeParallelMove (rdst, rsrc, count);
      for (int i = 0; i < n; ++i)
        if (src[i]->type != AOP_REG) genMove (dst[i], src[i], true);
      for (int i = n; i-- > 0;) freeAsmop (items[i]->left, NULL, items[i], TRUE);
      return true;
    }
  /* 先释放探测 asmop，再分两阶段快照。内存读可能使用 A/DPTR，但不能
     覆盖尚未保存的寄存器源。每次读完成后先弹回临时槽再释放地址临时量，
     防止 freeAsmop 把参数字节误当成保存的 R0/R1。 */
  for (int i = n; i-- > 0;) freeAsmop (items[i]->left, NULL, items[i], TRUE);
  reg_info *stackdest[8]; int bytes = 0;
  bool captured[2] = {false, false};
  for (int i = 0; i < n; ++i)
    {
      aopOp (items[i]->left, items[i], FALSE);
      if (items[i]->left->aop->type == AOP_REG)
        {
          for (int b = 0; b < dst[i]->size; ++b)
            {emitpush (items[i]->left->aop->aopu.aop_reg[b]->dname); stackdest[bytes++] = dst[i]->aopu.aop_reg[b];}
          captured[i] = true;
        }
      freeAsmop (items[i]->left, NULL, items[i], TRUE);
    }
  for (int i = 0; i < n; ++i)
    if (!captured[i])
      {
        aopOp (items[i]->left, items[i], FALSE);
        /* 固定符号地址通过 A 和 DPTR 读取，两个 DR 槽互不相交。
           所有寄存器源已保存，因此可直接填槽，避免内存值再次压栈。 */
        if (fixedSources)
          {
            genMove (dst[i], items[i]->left->aop, true);
            freeAsmop (items[i]->left, NULL, items[i], TRUE);
            continue;
          }
        for (int b = 0; b < dst[i]->size; ++b)
          {MOVA (opGet (items[i]->left, b, FALSE, FALSE)); emitpush ("acc");}
        for (int b = dst[i]->size; b-- > 0;) emitpop (dst[i]->aopu.aop_reg[b]->dname);
        freeAsmop (items[i]->left, NULL, items[i], TRUE);
        for (int b = 0; b < dst[i]->size; ++b)
          {emitpush (dst[i]->aopu.aop_reg[b]->dname); stackdest[bytes++] = dst[i]->aopu.aop_reg[b];}
      }
  while (bytes) emitpop (stackdest[--bytes]->dname);
  return true;
}

static void
genNativeMultiReceive (iCode *first)
{
  iCode *items[2]; asmop *src[2], *dst[2]; int n = 0, regs = 0;
  for (iCode *s = first; s && s->op == RECEIVE; s = s->next)
    if (!s->generated)
      {wassert (n < 2); items[n] = s; src[n++] = aopArg (currFunc->type, s->argreg);}
  /* 内存落点优先，保存整组输入跨过地址计算/写入，再处理寄存器落点。 */
  for (int i = 0; i < n; ++i)
    {
      aopOp (items[i]->result, items[i], FALSE);
      if (items[i]->result->aop->type != AOP_REG)
        {
          for (int j = 0; j < n; ++j) if (j != i && !items[j]->generated)
            for (int b = 0; b < src[j]->size; ++b) emitpush (src[j]->aopu.aop_reg[b]->dname);
          genMove (items[i]->result->aop, src[i], true);
          for (int j = n; j-- > 0;) if (j != i && !items[j]->generated)
            for (int b = src[j]->size; b-- > 0;) emitpop (src[j]->aopu.aop_reg[b]->dname);
          items[i]->generated = 1;
        }
      freeAsmop (items[i]->result, NULL, items[i], TRUE);
    }
  asmop *sources[2];
  for (int i = 0; i < n; ++i)
    if (!items[i]->generated)
      {aopOp (items[i]->result, items[i], FALSE); dst[regs] = items[i]->result->aop; sources[regs++] = src[i];}
  nativeParallelMove (dst, sources, regs);
  for (int i = n; i-- > 0;)
    {freeAsmop (items[i]->result, NULL, items[i], TRUE); items[i]->generated = 1;}
}
