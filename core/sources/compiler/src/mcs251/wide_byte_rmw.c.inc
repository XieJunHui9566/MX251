/* SPDX-License-Identifier: GPL-2.0-or-later
 * 相邻单字节 RMW 只复用 DR16 地址；不缓存或合并 volatile 事务。
 */
static bool
wmemory_byte_rmw (wide_function *f,int first)
{
  if(first+2>=f->nn)return false;
  wide_node *load=&f->nodes[first],*op=&f->nodes[first+1],*store=&f->nodes[first+2];
  if(load->op!=WM_LOAD || store->op!=WM_STORE ||
     (op->op!='^' && op->op!='|' && op->op!=BITWISEAND))return false;
  wide_node *mem[2]={load,store};
  for(int i=0;i<2;++i) {
    wide_node *n=mem[i];
    if(!n->byteAccess || n->width!=1 || n->pointerAccess || n->codeAccess ||
       n->addressObject>=0 || n->addressSymbol || n->addressOp || n->addressOffset || n->addressIndex.value>=0 ||
       n->left.width!=4 || n->left.value>=0 || n->left.floating || n->left.codePointer)return false;
  }
  if(!load->left.literal || load->left.literal>0x00ffffffUL ||
     load->left.literal!=store->left.literal || op->floating || op->byteAccess ||
     op->pointerAccess || op->codeAccess || op->addressSymbol || op->addressObject>=0 ||
     op->addressOp || op->addressOffset || op->addressIndex.value>=0 || op->width!=1 ||
     op->left.width!=1 || op->right.width!=1 || op->right.value>=0 ||
     op->right.floating || op->right.codePointer || op->left.value!=load->result ||
     store->right.value!=op->result || store->right.width!=1 ||
     store->right.floating || store->right.codePointer)return false;
  int values[2]={load->result,op->result};
  for(int i=0;i<2;++i) {
    if(values[i]<0 || values[i]>=f->nv)return false;
    wide_value *v=&f->values[values[i]];
    if(v->width!=1 || v->pointer || v->codePointer || v->floating || v->symbolSnapshot || v->reads!=1 ||
       v->reg<0 || v->reg>=16 || !mcs251RegisterEncodable(v->reg,1) ||
       (wide_mask(v->reg,1)&MCS251_AB_BYTES))return false;
  }
  if(!load->origin || f->values[values[0]].reg!=f->values[values[1]].reg)return false;
  for(int v=0;v<2;++v) {
    unsigned definitions=0;
    for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].def,values[v]))++definitions;
    if(definitions!=1)return false;
  }
  /* 不穿过独立入口、真实采样或不同循环权重。未知形态保持原发射路径。 */
  for(int i=0;i<3;++i) {
    wide_node *n=&f->nodes[first+i];
    if(!n->origin || n->label>=0 || n->target>=0 || n->orderedReadCount || n->orderedAsmCount ||
       n->args || n->stackArgs || n->stackBytes ||
       n->origin->depth!=load->origin->depth || n->succ[1]>=0)return false;
    if(i<2 && n->succ[0]!=first+i+1)return false;
  }
  for(int i=0;i<f->nn;++i)if(i!=first && i!=first+1)
    for(int j=0;j<2;++j)if(f->nodes[i].succ[j]==first+1 || f->nodes[i].succ[j]==first+2)return false;
  for(int j=0;j<f->nj;++j)
    if(f->jumpSuccessors[j]==first+1 || f->jumpSuccessors[j]==first+2)return false;
  int reg=f->values[values[0]].reg;
  const char *name=op->op=='^'?"xrl":op->op=='|'?"orl":"anl";
  /* 目的寄存器和位运算与原路径逐条相同。中间没有指令可改 DR16，
   * 因而第二次重复的地址装载可消去；不改变被访问地址、访问次数、
   * 宽度、顺序或前后寄存器值，也不把此序列声称为原子操作。 */
  wscratch_memory_address(f,load,3);
  we(f,"; wide adjacent byte-rmw address-reuse exact-read=1 exact-write=1 reg=%d\n",reg);
  we(f,"\tmov\tr%d,@dr16\n\t%s\tr%d,#0x%02lx\n\tmov\t@dr16,r%d\n",
     reg,name,reg,(unsigned long)(op->right.literal&255),reg);
  return true;
}
