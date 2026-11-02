/* SPDX-License-Identifier: GPL-2.0-or-later
 * 保留逐字节事务的低寄存器访存；不改变 volatile 宽度、顺序或次数。 */
static bool
wmemory_exact_byte (wide_function *f, wide_node *n)
{
  const char *enabled = getenv ("MX251_EXACT_BYTE_ACCESS");
  if (!enabled || strcmp (enabled, "1") || !n->byteAccess ||
      (n->width != 2 && n->width != 4) || n->codeAccess || n->pointerAccess ||
      n->addressObject >= 0 || n->addressSymbol ||
      (n->op != WM_LOAD && n->op != WM_STORE)) return false;
  bool store = n->op == WM_STORE;
  int value = store ? n->right.value : n->result;
  int reg = value >= 0 ? f->values[value].reg : -1;
  /* 地址先快照到保留 DR16；随后写目的低寄存器不会改掉地址。
   * R0..R7 与当前寄存器银行的 RAM 字节物理别名；任意间接地址可能
   * 命中该银行，逐字节直连会破坏尚未读取的源。因此保留原快照路径。
   * 只有专用 R8..R15 的精确同宽值可直接拆物理字节（仍排除 A/B）；
   * 扩展/常量/spill 仍经原 wload。 */
  bool direct = reg >= 8 && reg + n->width <= 16 &&
    !(wide_mask (reg, n->width) & wide_mask (10, 2)) &&
    (!store || n->right.width == n->width);
  if (store && !direct) wload (f, n->right, 20, n->width);
  wload (f, n->left, 16, 4);
  we (f, "; wide exact-byte %s width=%d direct=%d byte-order=ascending\n",
      store ? "store" : "load", n->width, direct);
  for (int k = 0; k < n->width; ++k)
    {
      if (direct)
        {
          if (store) we (f, "\tmov\t@dr16,r%d\n", reg+k);
          else we (f, "\tmov\tr%d,@dr16\n", reg+k);
        }
      else
        {
          if (store && !(k & 1)) we (f, "\tmov\twr10,wr%d\n", 20+k);
          if (store) we (f, "\tmov\t@dr16,r%d\n", 10+(k&1));
          else we (f, "\tmov\tr%d,@dr16\n", 10+(k&1));
          if (!store && (k&1)) we (f, "\tmov\twr%d,wr10\n", 20+k-1);
        }
      if (k + 1 < n->width) we (f, "\tinc\tdr16\n");
    }
  if (!store && !direct) wstore (f, n->result, 20);
  return true;
}
