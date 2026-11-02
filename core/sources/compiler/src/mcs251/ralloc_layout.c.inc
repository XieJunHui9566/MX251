/* MCS251 标量布局选择。生命周期和溢出仍由现有分配器管理；这里只在
 * 全部字节可用时选择整组布局，禁止为了凑组抢占活跃值或保留 A/B 别名。 */
/* 完整 DR 的发射成本低于两个 WR；同级优先保留低寄存器和指针寄存器。
 * 以宽位布局为候选单位，窄值保留原有顺序；不预留尚未使用的 DR。 */
static unsigned
mcs251LayoutCost (unsigned mask, int size, bool nativeDword)
{
  unsigned cost = size == 4 && !nativeDword ? 12 : 0;
  int i;
  if (size == 2) return 0;
  for (i = 0; i < 16; ++i)
    if (mask & (1u << i))
      {
        if (i >= 8) cost += 1;
        if (i < 2) cost += 4;
      }
  return cost;
}

static bool
mcs251LayoutAvailable (const symbol *sym, const int *bytes, int size, int assigned)
{
  int i;
  for (i = 0; i < size; ++i)
    {
      reg_info *reg = registerForOffset (bytes[i]);
      if (!reg || !registerAvailableToSymbol (reg, sym) ||
          (reg->type != REG_GPR && (mcs251_ptrRegReq || reg->type != REG_PTR)))
        return false;
      if (i < assigned)
        {
          if (sym->regs[i] != reg) return false;
        }
      else if (!reg->isFree)
        return false;
    }
  return true;
}

/* 同时比较完整 DR 与两个 WR 的布局。逻辑最低字节放在物理高编号端，
 * 与目标大端寄存器文件及现有标量字节序约定一致。首字节提交前验证全组；
 * 后续调用只能沿用已提交的前缀，不能中途改变逻辑字节位置。 */
static reg_info *
allocNativeTupleByte (short type, const symbol *sym)
{
  static const int words[] = {7, 5, 3, 15, 13, 9, 1};
  int assigned = 0, i, j, size = sym->nRegs;
  reg_info *best = NULL;
  unsigned bestCost = ~0u;
  if (type != REG_GPR || IS_AGGREGATE (sym->type) || (size != 2 && size != 4))
    return NULL;
  while (assigned < size && sym->regs[assigned]) ++assigned;
  if (assigned == size) return NULL;
  for (i = 0; i < 7; ++i)
    for (j = 0; j < (size == 4 ? 7 : 1); ++j)
      {
        int bytes[4] = {words[i], words[i] - 1, words[j], words[j] - 1};
        unsigned mask = (3u << (words[i] - 1));
        unsigned cost;
        bool nativeDword = false;
        if (size == 4)
          {
            if (words[i] <= words[j]) continue;
            mask |= 3u << (words[j] - 1);
            nativeDword = words[i] % 4 == 3 && words[j] == words[i] - 2;
          }
        if (!mcs251LayoutAvailable (sym, bytes, size, assigned)) continue;
        cost = mcs251LayoutCost (mask, size, nativeDword);
        if (!best || cost < bestCost)
          {
            best = registerForOffset (bytes[assigned]);
            bestCost = cost;
          }
      }
  if (best)
    {
      best->isFree = 0;
      if (currFunc)
        currFunc->regsUsed = bitVectSetBit (currFunc->regsUsed, best->rIdx);
    }
  return best;
}
