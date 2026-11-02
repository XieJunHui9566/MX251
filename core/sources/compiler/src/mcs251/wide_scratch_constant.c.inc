/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅在两个已证明的 MIR 发射模板间保持物理 DR16 常量；不是文本层
 * 放宽间接访存 effects。Intel MCS-251 3.3: R16..R31 只经寄存器名访问，
 * 银行 RAM 别名为 R0..R7；本规则还拒绝整个低 64K、未知及计算地址。
 * A/R11、B/R10、DPX、SPX 不缓存；不删除 SUB 清零或任何 flags 定义。
 */
static bool
wscratch_memory_proven(wide_function *f,wide_node *n,int span)
{
  int first=f->scratchNode,last=first+span-1;
  if(!mcs251ScratchConstantRetentionEnabled || IFFUNC_ISISR(currFunc->type) ||
     first<0 || last>=f->nn || n!=&f->nodes[first] || (span!=1 && span!=3) ||
     !n->byteAccess || n->width!=1 || n->pointerAccess || n->codeAccess ||
     n->addressObject>=0 || n->addressSymbol || n->addressOp || n->addressOffset ||
     n->addressIndex.value>=0 || n->left.width!=4 || n->left.value>=0 ||
     n->left.floating || n->left.codePointer ||
     n->left.literal<0x00010000UL || n->left.literal>0x00ffffffUL)return false;
  /* 专用模板没有隐藏入口、嵌套调用、真实采样或有序汇编。
   * 即便前驱是紧邻事务，也必须排除另一个 CFG/jump-table 入口。 */
  for(int i=first;i<=last;++i) {
    wide_node *p=&f->nodes[i];
    if(!p->origin || p->label>=0 || p->target>=0 || p->orderedReadCount ||
       p->orderedAsmCount || p->args || p->stackArgs || p->stackBytes ||
       p->succ[1]>=0 || p->succ[0]!=i+1)return false;
    for(int j=0;j<f->nn;++j)if(j!=i-1)
      for(int k=0;k<2;++k)if(f->nodes[j].succ[k]==i)return false;
    for(int j=0;j<f->nj;++j)if(f->jumpSuccessors[j]==i)return false;
  }
  return true;
}

static void
wscratch_memory_address(wide_function *f,wide_node *n,int span)
{
  if(!wscratch_memory_proven(f,n,span)) {
    f->scratchAddressValid=false;wload(f,n->left,16,4);return;
  }
  uint32_t address=n->left.literal;
  bool reuse=f->scratchAddressValid && f->scratchNextNode==f->scratchNode;
  unsigned removed=0;
  for(int half=0;half<2;++half) {
    unsigned shift=half?0:16;
    unsigned part=(address>>shift)&0xffff;
    if(reuse && part==((f->scratchAddress>>shift)&0xffff))++removed;
    else we(f,"\tmov\twr%d,#0x%04x\n",16+2*half,part);
  }
  if(removed)we(f,"; wide scratch retained address=0x%06lx node=%d removed-mov=%u exact-transactions=%d\n",
    (unsigned long)address,f->scratchNode,removed,span==3?2:1);
  f->scratchAddress=address;f->scratchAddressValid=true;f->scratchNextNode=f->scratchNode+span;
}
