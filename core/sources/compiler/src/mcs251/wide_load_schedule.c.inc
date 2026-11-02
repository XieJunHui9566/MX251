/* SPDX-License-Identifier: GPL-2.0-or-later
 * 最后一次普通读取提前到同一直线片段，缩短地址值与后续地址的重叠。
 * 保留全部内存事务；只越过纯值计算和普通读取，不越过写入或同步。
 * 这是独立候选，最终仍须经过完整发射成本和逐循环路径保护。
 */
static bool
wide_schedule_load(wide_function *f,wide_node *n)
{
  if(n->op!=WM_LOAD || n->result<0 || n->result>=f->nv ||
     n->left.value<0 || n->left.value>=f->nv || n->left.width!=4 ||
     n->addressObject>=0 || n->addressOp || n->addressIndex.value>=0 ||
     n->byteAccess || n->codeAccess || n->pointerAccess || n->floating ||
     n->args || n->stackArgs || n->right.value>=0 ||
     (n->width!=1 && n->width!=2 && n->width!=4))return false;
  wide_value *a=&f->values[n->left.value],*r=&f->values[n->result];
  if(!a->pointer || a->codePointer || a->symbolSnapshot || a->width!=4 ||
     a->floating || r->pointer || r->codePointer || r->symbolSnapshot ||
     r->floating || r->pin>=0 || r->width!=n->width)return false;
  for(int v=0;v<f->nv;++v)
    if(wb_has(n->use,v)!=(v==n->left.value) ||
       wb_has(n->def,v)!=(v==n->result))return false;
  return true;
}

static bool
wide_schedule_pure(wide_node *n)
{
  if(n->byteAccess || n->codeAccess || n->pointerAccess || n->floating ||
     n->left.floating || n->right.floating ||
     n->args || n->stackArgs)return false;
  return n->op==WM_NOP || n->op==CAST || n->op=='=' || n->op=='+' ||
    n->op=='-' || n->op=='^' || n->op=='|' || n->op==BITWISEAND;
}

static void
wide_load_move_up(wide_function *f,int from,int to)
{
  int mapped[WN_MAX],original[WN_MAX];
  for(int i=0;i<f->nn;++i)mapped[i]=i;
  mapped[from]=to;for(int i=to;i<from;++i)mapped[i]=i+1;
  for(int i=0;i<f->nn;++i)original[mapped[i]]=i;
  wide_node moving=f->nodes[from];
  memmove(&f->nodes[to+1],&f->nodes[to],(from-to)*sizeof f->nodes[0]);
  f->nodes[to]=moving;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];bool fall=wide_sink_fallthrough(n,original[i]);
    for(int k=0;k<2;++k)if(n->succ[k]>=0)
      n->succ[k]=(k==0 && fall)?i+1:mapped[n->succ[k]];
  }
  for(int i=0;i<f->nj;++i)f->jumpSuccessors[i]=mapped[f->jumpSuccessors[i]];
}

static unsigned
wide_schedule_last_loads(wide_function *f)
{
  unsigned moved=0;
  /* 每函数只形成一个局部移动候选，避免继续使用移动前的活跃集。
   * 扫描固定且有界；不复制读取或穿越基本块。 */
  for(int value=0;value<f->nv;++value) {
    int at=-1,definitions=0;
    for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].def,value)){at=i;++definitions;}
    if(definitions!=1 || !wide_schedule_load(f,&f->nodes[at]))continue;
    wide_node *load=&f->nodes[at];int address=load->left.value;
    /* 只考虑地址的末次使用；该地址在回边上的使用同样排除。 */
    if(wb_has(load->out,address))continue;
    int to=at;unsigned reads=0;
    for(int j=at-1;j>=0;--j) {
      wide_node *n=&f->nodes[j];bool ordinary=wide_schedule_load(f,n);
      if(n->succ[0]!=j+1 || n->succ[1]>=0 || n->jumpCount ||
         (!ordinary && !wide_schedule_pure(n)) ||
         wb_has(n->def,address) || wb_has(n->def,value) || wb_has(n->use,value))break;
      to=j;reads+=ordinary;
    }
    if(!reads || to==at)continue;
    bool valid=true;
    /* 边界两端也不能有侧入口，避免提前读取一条原本不执行的路径。 */
    for(int i=0;i<f->nn && valid;++i)for(int k=0;k<2;++k) {
      int target=f->nodes[i].succ[k];
      if(target>=to && target<=at && !(k==0 && wide_sink_fallthrough(&f->nodes[i],i)))valid=false;
    }
    for(int i=0;i<f->nj;++i)if(f->jumpSuccessors[i]>=to && f->jumpSuccessors[i]<=at)valid=false;
    if(!valid)continue;
    wide_load_move_up(f,at,to);++moved;
    dbuf_printf(f->diagnostics,"; wide load-schedule value=%d address=%d from=%d to=%d crossed-reads=%u\n",value,address,at,to,reads);
    break;
  }
  if(moved)wide_reset_allocation(f);
  return moved;
}
