/* SPDX-License-Identifier: GPL-2.0-or-later
 * 完整私有 union 的同块写后读转发。只消除已证明不可观察的本地对象，
 * 不放宽前端 CAST/volatile/atomic 规则，也不缓存跨调用的内存内容。 */
static bool
wide_union_forward_access(wide_function *f,wide_node *n,int object)
{
  return object>=0 && object<f->no && f->objects[object].privateUnion &&
    f->objects[object].size==4 && n->addressObject==object &&
    (!n->addressSymbol || n->addressSymbol==f->objects[object].sym) &&
    n->privateUnionWordAccess && n->width==4 && !n->addressOffset && n->left.value<0 &&
    n->addressIndex.value<0 && !n->addressOp && wide_private_union_memory(n);
}

static bool
wide_union_forward_pure(wide_function *f,wide_node *n)
{
  if(n->op==WM_LOAD || n->op==WM_STORE)
    return wide_union_forward_access(f,n,n->addressObject);
  /* 其它私有对象的完整访问可保留原序；未知内存、真实 volatile/bit/SFR、
   * 调用和控制流都截断本候选。这里不是任意内存无别名的推断器。 */
  if(n->byteAccess || n->codeAccess || n->pointerAccess)return false;
  switch(n->op) {
  case WM_NOP:case SEND:case WM_ADDRESS:case WM_PARAM_ADDRESS:
  case '=':case CAST:case '+':case '-':case '^':case '|':case BITWISEAND:
  case LEFT_OP:case RIGHT_OP:case '!':case GETABIT:case GETBYTE:case GETWORD:
  case '<':case '>':case LE_OP:case GE_OP:case EQ_OP:case NE_OP:
  case WM_UMULW:return true;
  default:return false;
  }
}

static unsigned
wide_private_union_forward(wide_function *f)
{
  const char *enabled=getenv("MX251_PRIVATE_UNION_FORWARD");
  if(!enabled || strcmp(enabled,"1") || !f->no)return 0;
  /* 删槽改变整个函数的 SP/入口 FLAGS，观察不一定发生在写后读区间。
   * 因此全函数只接纳私有对象事务和纯值操作；真实 SFR/bit/符号内存、
   * 参数内存及其它未知访问均回退。调用还须在已加载的闭合 ABI 契约
   * 中；契约只是类型/目标边界，宿主仍必须证明传递调用无 SP/PSW/ASM
   * 观察、入口 FLAGS 独立和 ISR 保存，不能将它伪称为完整语义证明。 */
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==WM_NOP)continue;
    if(n->op==CALL && (!mcs251InternalNative(currFunc) || !mcs251InternalNative(n->callee)))return 0;
    if(n->op==WM_LOAD || n->op==WM_STORE) {
      if(!wide_union_forward_access(f,n,n->addressObject))return 0;
    } else if(n->byteAccess || n->codeAccess || n->pointerAccess ||
              (n->op>=WM_LOAD && n->op!=WM_ADDRESS && n->op!=WM_UMULW))return 0;
  }
  bool removed[WO_MAX]={false};int store[WO_MAX],last[WO_MAX],reads[WO_MAX];
  wide_operand snapshot[WO_MAX];int predecessors[WN_MAX]={0},only[WN_MAX];
  unsigned count=0;
  for(int i=0;i<f->nn;++i)only[i]=-1;
  /* 不以列表相邻冒充支配：区间每个节点只能从其紧邻前驱到达，所有
   * 显式分支和跳转表的边也参与检查。标签/调用本身仍保守拒绝。 */
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    for(int k=0;k<2+n->jumpCount;++k) {
      int next=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
      if(next<0)continue;
      if(next>=f->nn)return 0;
      ++predecessors[next];only[next]=i;
    }
  }
  for(int o=0;o<f->no;++o) {
    store[o]=last[o]=-1;reads[o]=0;snapshot[o]=(wide_operand){.value=-1};
    if(!f->objects[o].privateUnion || !wide_private_union_type(&f->objects[o]))continue;
    bool valid=true;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->op==WM_NOP)continue;
      if(n->addressObject!=o && n->addressSymbol!=f->objects[o].sym)continue;
      if(!wide_union_forward_access(f,n,o)) {valid=false;break;}
      if(n->op==WM_STORE) {
        int source=n->right.value;
        if(store[o]>=0 || reads[o] || source<0 || source>=f->nv || n->right.width!=4 ||
           f->values[source].width!=4 || f->values[source].pointer ||
           f->values[source].codePointer || f->values[source].symbolSnapshot) {valid=false;break;}
        store[o]=i;snapshot[o]=n->right;
      } else if(n->op==WM_LOAD) {
        if(store[o]<0 || n->result<0 || n->result>=f->nv || f->values[n->result].width!=4 ||
           f->values[n->result].pointer || f->values[n->result].codePointer ||
           f->values[n->result].symbolSnapshot) {valid=false;break;}
        last[o]=i;++reads[o];
      } else {valid=false;break;}
    }
    if(!valid || store[o]<0 || !reads[o])continue;
    for(int i=store[o]+1;i<=last[o];++i) {
      wide_node *n=&f->nodes[i],*previous=&f->nodes[i-1];
      if(predecessors[i]!=1 || only[i]!=i-1 || previous->succ[0]!=i ||
         previous->succ[1]>=0 || previous->jumpCount ||
         !wide_union_forward_pure(f,n) || wb_has(n->def,snapshot[o].value)) {valid=false;break;}
    }
    if(valid) {removed[o]=true;++count;}
  }
  if(!count)return 0;
  /* 先完整验证对象，再逐 LOAD 写入独立的位复制节点。源值在区间内
   * 不可重定义；LOAD 结果仍是自己的虚拟值，后续跨 CALL 活期由原
   * 分配/保存逻辑处理，绝不假定 DR4/DR12 等物理位置能跨调用保留。 */
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int o=n->addressObject;
    if(n->op==WM_NOP) {n->addressObject=-1;n->addressSymbol=NULL;continue;}
    if(o<0 || !removed[o])continue;
    if(n->op==WM_LOAD) {
      n->op=WM_RAW32_COPY;n->right=snapshot[o];
      n->use=(wide_bits){{0}};wb_set(&n->use,n->right.value);
    } else {
      wassert(n->op==WM_STORE && i==store[o]);
      n->op=WM_NOP;n->result=-1;n->right=(wide_operand){.value=-1};
      n->use=n->def=(wide_bits){{0}};
    }
    n->left=n->addressIndex=(wide_operand){.value=-1};
    n->addressObject=-1;n->addressSymbol=NULL;n->addressOffset=0;n->addressOp=0;
    n->byteAccess=n->codeAccess=n->pointerAccess=n->privateUnionWordAccess=false;
  }
  wide_remove_dead_addresses(f);
  int map[WO_MAX],objects=0,frame=0;
  for(int o=0;o<f->no;++o) {
    if(removed[o]) {
      map[o]=-1;
      dbuf_printf(f->diagnostics,"; wide private-union raw32-forward object=%d store=%d reads=%d bytes=4 same-block=1 cross-call=0\n",o,store[o],reads[o]);
    } else {
      map[o]=objects;wide_object object=f->objects[o];object.slot=frame;
      frame+=object.size;f->objects[objects++]=object;
    }
  }
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==WM_NOP) {n->addressObject=-1;n->addressSymbol=NULL;continue;}
    if(n->addressObject>=0) {wassert(map[n->addressObject]>=0);n->addressObject=map[n->addressObject];}
  }
  f->no=objects;f->frame=frame;
  wide_reset_allocation(f);
  return count;
}
