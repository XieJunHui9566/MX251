/* SPDX-License-Identifier: GPL-2.0-or-later
 * 单字节事务直接连接字节值，不扩大 volatile 访问、不缓存访问结果。
 */
static bool
wmemory_single_byte (wide_function *f, wide_node *n)
{
  if (!n->byteAccess || n->width != 1 || n->pointerAccess || n->codeAccess ||
      n->addressObject >= 0 || n->addressSymbol || n->addressOp ||
      n->left.width != 4 || (n->op != WM_LOAD && n->op != WM_STORE)) return false;
  bool store = n->op == WM_STORE;
  int value = store ? n->right.value : n->result;
  if (store && (n->right.width != 1 || n->right.floating || n->right.codePointer)) return false;
  if (value >= 0 && (value >= f->nv || f->values[value].width != 1 ||
      f->values[value].pointer || f->values[value].floating)) return false;
  if (!store && value < 0) return false;
  int reg = value >= 0 ? f->values[value].reg : -1;
  if (reg >= 0 && (!mcs251RegisterEncodable (reg, 1) || reg >= 16 ||
      (wide_mask (reg, 1) & MCS251_AB_BYTES))) return false;

  /* DR16 是不可分配 scratch，先完成地址快照。源与地址分配可以重叠；
   * 加载结果也可以覆盖原地址寄存器，因为之后只有这一笔事务。
   * R0..R7 与寄存器银行 RAM 的别名不会跨第二个字节读写传播：store
   * 在唯一写入前取样源，load 在唯一读取后写目的。故不同于多字节
   * 拆搬，允许低银行寄存器；常量和 spill 仅借用保留 A，仍不触及 B。
   * 宽输入截断、指针/代码/栈专用路径及索引地址继续走原有实现。
   */
  wscratch_memory_address (f, n, 1);
  we (f, "; wide single-byte %s direct-reg=%d value=%s exact-transactions=1\n",
      store ? "store" : "load", reg, value < 0 ? "literal" : reg < 0 ? "spill" : "register");
  if (reg >= 0)
    {
      if (store) we (f, "\tmov\t@dr16,r%d\n", reg);
      else we (f, "\tmov\tr%d,@dr16\n", reg);
    }
  else
    {
      char slot[32];
      if (value >= 0) wa (slot, f->values[value].slot - f->frame - f->stackDepth + 1);
      if (store)
        {
          if (value < 0) we (f, "\tmov\ta,#0x%02lx\n", (unsigned long)(n->right.literal & 255));
          else we (f, "\tmov\ta,%s\n", slot);
          we (f, "\tmov\t@dr16,a\n");
        }
      else we (f, "\tmov\ta,@dr16\n\tmov\t%s,a\n", slot);
    }
  return true;
}
