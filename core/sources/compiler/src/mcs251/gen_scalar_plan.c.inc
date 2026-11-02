/* Native scalar transfer and arithmetic plans. GPL-2.0-or-later.
   Included in gen.c after native register layout helpers. */
typedef struct
{
  const char *dword;
  const char *words[2];
  char memory[2][32];
  bool stack;
  int size;
  int instructions;
  int bytes;
} mcs251_scalar_location;

/* 先完整验证并构造搬运计划，再发射任何指令。逻辑字节编号从低位开始，
   SPX 地址和 WR/DR 内部均为大端。禁止扩宽 volatile、聚合及原子访问。 */
static bool
mcs251PlanScalarLocation (operand *op, mcs251_scalar_location *p)
{
  const asmop *aop = AOP (op);
  memset (p, 0, sizeof (*p));
  p->size = aop->size;
  if ((p->size != 2 && p->size != 4) || aop->aop_is_volatile ||
      aop->aop_is_aggregate || isOperandVolatile (op, TRUE) ||
      isAtomic (operandType (op)))
    return FALSE;
  if (aop->type == AOP_REG)
    {
      for (int i = 0; i < p->size; ++i)
        if (aop->aopu.aop_reg[i]->offset < 0 ||
            aop->aopu.aop_reg[i]->offset > 15 ||
            aop->aopu.aop_reg[i]->offset == 10 ||
            aop->aopu.aop_reg[i]->offset == 11)
          return FALSE;
      for (int i = 0; i < p->size / 2; ++i)
        {
          p->words[i] = mcs251WordForNativePair (aop, 2 * i);
          if (!p->words[i] || (i && !strcmp (p->words[0], p->words[i])))
            return FALSE;
        }
      if (p->size == 4)
        p->dword = mcs251DwordForNativeTuple (aop);
      p->instructions = p->dword ? 1 : p->size / 2;
      p->bytes = p->instructions * 2;
      return TRUE;
    }
  if (aop->type != AOP_MCS251_STK)
    return FALSE;
  p->stack = TRUE;
  for (int i = 0; i < p->size / 2; ++i)
    {
      int displacement = stackoffset (aop->aopu.aop_sym) +
                          mcs251ScalarByteOffset (aop, 2 * i + 1);
      if (displacement < -32768 || displacement >= 32767)
        return FALSE;
      mcs251FormatStackOperand (p->memory[i], sizeof (p->memory[i]), displacement);
    }
  p->instructions = p->size / 2;
  p->bytes = p->instructions * 4;
  return TRUE;
}

static void mcs251EmitScalarTransfer (const mcs251_scalar_location *, int, bool);

/* 常量算术保留完整 WR，不再拆成两次 A/进位搬运。只处理普通整数
 * 寄存器对；不扩大 volatile 访问、位宽、指针和特殊入口的契约。 */
static bool
mcs251GenWordLiteral (operand *left, operand *right, operand *result, bool subtract)
{
  const char *source, *destination;
  if (!_G.currentFunc || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) || FUNC_REGBANK (_G.currentFunc->type) ||
      AOP_SIZE (left) != 2 || AOP_SIZE (result) != 2 || AOP_TYPE (right) != AOP_LIT || AOP_SIZE (right) > 2 ||
      !IS_INTEGRAL (operandType (left)) || !IS_INTEGRAL (operandType (right)) ||
      !IS_INTEGRAL (operandType (result)) || IS_BITINT (operandType (left)) ||
      IS_BITINT (operandType (right)) || IS_BITINT (operandType (result)) || isOperandVolatileOrAtomic (left, TRUE) ||
      isOperandVolatileOrAtomic (result, TRUE)) return FALSE;
  source = mcs251WordForNativePair (AOP (left), 0);
  destination = mcs251WordForNativePair (AOP (result), 0);
  if (!source || !destination || !strcmp (source, "wr10") || !strcmp (destination, "wr10")) return FALSE;
  unsigned literal = (unsigned) ullFromVal (AOP (right)->aopu.aop_lit) & 0xffffu;
  if (!literal) return FALSE;
  emitcode (";", "mcs251 native word literal %s", subtract ? "sub" : "add");
  if (strcmp (source, destination)) emitcode ("mov", "%s,%s", destination, source);
  if (literal == 1 || literal == 2 || literal == 4)
    emitcode (subtract ? "dec" : "inc", "%s,#%u", destination, literal);
  else emitcode (subtract ? "sub" : "add", "%s,#0x%04x", destination, literal);
  return TRUE;
}

/* 已知 24 位地址加无符号窄索引：低 16 位用 WR，相同进位接入高字节。
 * 高字节仍使用 ASxxxx 字节重定位，不假定 CODE/XDATA 位于低 64 KiB。
 * 完整寄存器对相同或互不重叠，先取索引再直接写最终低字，不更改栈。 */
static bool
mcs251GenIndexedWordAddress (operand *left, operand *right, operand *result)
{
  operand *base = left, *index = right;
  const char *pair, *source;
  if (AOP_TYPE (base) != AOP_IMMD) { base = right; index = left; }
  if (!_G.currentFunc || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) || FUNC_REGBANK (_G.currentFunc->type) ||
      AOP_TYPE (base) != AOP_IMMD || AOP_SIZE (base) != 3 ||
      AOP (base)->aopu.aop_immd.from_cast_remat || AOP (base)->aopu.aop_immd.mcs251_pdata_page ||
      !AOP (base)->aopu.aop_immd.aop_immd1 || !AOP (base)->aopu.aop_immd.aop_immd1[0] ||
      !IS_PTR (operandType (base)) || !IS_PTR (operandType (result)) ||
      DCL_TYPE (operandType (base)) != DCL_TYPE (operandType (result)) ||
      (DCL_TYPE (operandType (result)) != FPOINTER && DCL_TYPE (operandType (result)) != CPOINTER) ||
      AOP_TYPE (result) != AOP_REG || AOP_SIZE (result) != 3 ||
      AOP_TYPE (index) != AOP_REG || (AOP_SIZE (index) != 1 && AOP_SIZE (index) != 2) ||
      !IS_INTEGRAL (operandType (index)) || IS_BITINT (operandType (index)) ||
      !SPEC_USIGN (getSpec (operandType (index))) ||
      isOperandVolatileOrAtomic (base, TRUE) || isOperandVolatileOrAtomic (index, TRUE) ||
      isOperandVolatileOrAtomic (result, TRUE)) return FALSE;
  for (int i = 0; i < 3; ++i) {
    int r = AOP (result)->aopu.aop_reg[i]->offset;
    if (r < 0 || r > 15 || r == 10 || r == 11) return FALSE;
  }
  pair = mcs251WordForNativePair (AOP (result), 0);
  source = AOP_SIZE (index) == 2 ? mcs251WordForNativePair (AOP (index), 0) :
           AOP (index)->aopu.aop_reg[0]->name;
  if (!pair || !source) return FALSE;
  if (AOP (result)->aopu.aop_reg[2]->offset == AOP (result)->aopu.aop_reg[0]->offset ||
      AOP (result)->aopu.aop_reg[2]->offset == AOP (result)->aopu.aop_reg[1]->offset) return FALSE;
  for (int i = 0; i < AOP_SIZE (index); ++i) {
    int r = AOP (index)->aopu.aop_reg[i]->offset;
    if (r < 0 || r > 15 || r == 10 || r == 11) return FALSE;
  }
  emitcode (";", "mcs251 indexed word address %d-bit", AOP_SIZE (index) * 8);
  if (AOP_SIZE (index) == 1 || strcmp (pair, source))
    emitcode (AOP_SIZE (index) == 2 ? "mov" : "movz", "%s,%s", pair, source);
  emitcode ("add", "%s,#%s", pair, AOP (base)->aopu.aop_immd.aop_immd1);
  emitcode ("clr", "a");
  emitcode ("addc", "a,%s", opGet (base, 2, FALSE, FALSE));
  opPut (result, "a", 2);
  return TRUE;
}

/* 小常量左移复用同一搬运描述。较大的计数已有按字节移动的专用路径，
   不用重复加倍替换它；有符号溢出及超宽移位也不新增语言语义。 */
static bool
mcs251GenScalarShiftPlan (operand *left, operand *result, unsigned count)
{
  mcs251_scalar_location l, d;
  int instructions, bytes;
  if (!count || count > 3 || !_G.currentFunc ||
      IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) ||
      !mcs251PlanScalarLocation (left, &l) ||
      !mcs251PlanScalarLocation (result, &d) || l.size != d.size)
    return FALSE;
  /* 完整且对齐的寄存器组之间只能相同或互不重叠，目标可直接作为
   * 左移工作寄存器。分散字组/栈位置继续先快照，避免交叉覆盖。 */
  if (!l.stack && !d.stack && (d.size == 2 || (l.dword && d.dword)))
    {
      const char *source = d.size == 2 ? l.words[0] : l.dword;
      const char *destination = d.size == 2 ? d.words[0] : d.dword;
      emitcode (";", "mcs251 direct scalar shift %d-bit by %u", 8 * d.size, count);
      if (strcmp (source, destination)) emitcode ("mov", "%s,%s", destination, source);
      for (unsigned i = 0; i < count; ++i) emitcode ("add", "%s,%s", destination, destination);
      return TRUE;
    }
  instructions = l.instructions + d.instructions + count;
  bytes = l.bytes + d.bytes + 2 * count;
  if (instructions >= 3 * d.size ||
      (optimize.codeSize && bytes >= 3 * d.size + (l.stack + d.stack) * d.size * 3))
    return FALSE;
  emitcode (";", "mcs251 scalar plan shl %d-bit by %u", 8 * d.size, count);
  mcs251EmitScalarTransfer (&l, 16, FALSE);
  for (unsigned i = 0; i < count; ++i)
    emitcode ("add", "%s16,%s16", d.size == 4 ? "dr" : "wr", d.size == 4 ? "dr" : "wr");
  mcs251EmitScalarTransfer (&d, 16, TRUE);
  return TRUE;
}

static void
mcs251EmitScalarTransfer (const mcs251_scalar_location *p, int scratch, bool store)
{
  if (p->dword)
    {
      if (store) emitcode ("mov", "%s,dr%d", p->dword, scratch);
      else emitcode ("mov", "dr%d,%s", scratch, p->dword);
      return;
    }
  for (int i = 0; i < p->size / 2; ++i)
    {
      int word = scratch + p->size - 2 - i * 2;
      const char *location = p->stack ? p->memory[i] : p->words[i];
      if (store) emitcode ("mov", "%s,wr%d", location, word);
      else emitcode ("mov", "wr%d,%s", word, location);
    }
}

/* 无符号比较只需原生 CMP 产生的借位标志，结果交给既有分支/布尔值
   生成路径。先快照两个输入，不破坏操作数；有符号和窄 _BitInt 仍走原路。
   和算术共用位置描述，避免分配器选择了 WR 对而比较仍被拆成四个字节。 */
static bool
mcs251GenScalarComparePlan (operand *left, operand *right, int sign)
{
  mcs251_scalar_location l, r;
  int instructions, bytes;
  /* 无符号 WR 与常量比较直接产生借位；不需要 A 的两次 SUBB。
   * 低字节为零时旧路径可能只需一次字节比较，仍保留该捷径。 */
  if (!sign && _G.currentFunc && !IFFUNC_ISNAKED (_G.currentFunc->type) &&
      !IFFUNC_CALLEESAVES (_G.currentFunc->type) && !FUNC_REGBANK (_G.currentFunc->type) &&
      AOP_SIZE (left) == 2 && AOP_SIZE (right) <= 2 && AOP_TYPE (right) == AOP_LIT &&
      IS_INTEGRAL (operandType (left)) && IS_INTEGRAL (operandType (right)) &&
      !IS_BITINT (operandType (left)) && !IS_BITINT (operandType (right)) &&
      !isOperandVolatileOrAtomic (left, TRUE))
    {
      const char *reg = mcs251WordForNativePair (AOP (left), 0);
      unsigned literal = (unsigned) ullFromVal (AOP (right)->aopu.aop_lit) & 0xffffu;
      if (reg && strcmp (reg, "wr10") && (literal & 0xffu))
        {
          emitcode (";", "mcs251 native word literal compare");
          emitcode ("cmp", "%s,#0x%04x", reg, literal);
          return TRUE;
        }
    }
  if (sign || !_G.currentFunc || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) ||
      IS_BITINT (operandType (left)) || IS_BITINT (operandType (right)) ||
      !mcs251PlanScalarLocation (left, &l) ||
      !mcs251PlanScalarLocation (right, &r) || l.size != r.size)
    return FALSE;
  instructions = l.instructions + r.instructions + 1;
  bytes = l.bytes + r.bytes + 2;
  if (instructions >= 3 * l.size ||
      (optimize.codeSize && bytes >= 3 * l.size + (l.stack + r.stack) * l.size * 3))
    return FALSE;
  emitcode (";", "mcs251 scalar compare plan %d-bit", 8 * l.size);
  mcs251EmitScalarTransfer (&l, 16, FALSE);
  mcs251EmitScalarTransfer (&r, 20, FALSE);
  emitcode ("cmp", "%s16,%s20", l.size == 4 ? "dr" : "wr", l.size == 4 ? "dr" : "wr");
  return TRUE;
}

/* ABI2 不长期分配 DR16/DR20；普通中断入口保存这些寄存器。先快照两个
   输入，再写结果，故交叉重叠和栈别名不依赖字节分配顺序。这里不越过
   iCode、调用或内存屏障，也不改变调用约定。特殊手写入口保留旧路径。
   成本是指令数/编码字节的保守估计，不能冒充芯片实测周期。 */
static bool
mcs251GenScalarPlan (iCode *ic, operand *left, operand *right,
                     operand *result, const char *instruction)
{
  mcs251_scalar_location l, r, d;
  int instructions, bytes, byteInstructions, byteBytes;
  if (!_G.currentFunc || IFFUNC_ISNAKED (_G.currentFunc->type) ||
      IFFUNC_CALLEESAVES (_G.currentFunc->type) ||
      !mcs251PlanScalarLocation (left, &l) ||
      !mcs251PlanScalarLocation (right, &r) ||
      !mcs251PlanScalarLocation (result, &d) ||
      l.size != r.size || l.size != d.size)
    return FALSE;
  instructions = l.instructions + r.instructions + d.instructions + 1;
  bytes = l.bytes + r.bytes + d.bytes + 2;
  byteInstructions = 3 * d.size;
  byteBytes = byteInstructions + (l.stack + r.stack + d.stack) * d.size * 3;
  /* ABI4 的原生模式中，寄存器逐字节 MOV/ADD/ADDC 至少两字节：
   * 旧 8051 指令带 A5 前缀，新寄存器指令也为两字节。旧成本把它们
   * 当成一字节，会在减少溢出后反而选中更大、更慢的字节算术。
   * SPX 零位移字节访问为三字节，带位移为四字节；按三字节下界
   * 计费，避免夸大原生计划收益。 */
  if (FUNC_SDCCCALL (_G.currentFunc->type) == 4)
    byteBytes = 2 * byteInstructions + (l.stack + r.stack + d.stack) * d.size;
  if (instructions >= byteInstructions || (optimize.codeSize && bytes >= byteBytes))
    return FALSE;
  emitcode (";", "mcs251 scalar plan %s %d-bit (%d instructions, estimated %d bytes)",
            instruction, 8 * d.size, instructions, bytes);
  mcs251EmitScalarTransfer (&l, 16, FALSE);
  mcs251EmitScalarTransfer (&r, 20, FALSE);
  emitcode (instruction, "%s16,%s20", d.size == 4 ? "dr" : "wr", d.size == 4 ? "dr" : "wr");
  mcs251EmitScalarTransfer (&d, 16, TRUE);
  return TRUE;
}
