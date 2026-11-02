/* 有界无符号仿射递推。仅处理已导入的纯 low32 乘法，不以用户函数名
 * 判断语义。固定计数器、唯一入口和写入位置同时成立才构造候选。
 * 原本的地址计算、内存事务、计数器和分支全部保留。 */
static bool
wide_ua_scalar(wide_function *f,wide_operand o,int width)
{
  if(o.width!=width || o.sign || o.floating || o.codePointer)return false;
  return o.value<0 || wide_affine_value(f,o.value,width,false);
}

static bool
wide_ua_definition(wide_function *f,int value,int at)
{
  int count=0;
  for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].def,value)) {
    if(i!=at)return false;
    ++count;
  }
  return count==1;
}

static bool
wide_ua_uses(wide_function *f,int value,int at)
{
  int count=0;
  for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].use,value)) {
    if(i!=at)return false;
    ++count;
  }
  return count==1;
}

/* 首版只接受来自入口 RECEIVE 的不变参数或字面量。这样不需要把
 * 内存采样或可能异常的运算提升到循环外；参数对象本身不能逃逸。 */
static bool
wide_ua_invariant(wide_function *f,wide_operand o,int preheader,
                  wide_address_dominance *d)
{
  if(!wide_ua_scalar(f,o,4))return false;
  if(o.value<0)return true;
  if(d->definitions[o.value]!=1 || !wide_address_dominates(f,d,o.value,preheader))return false;
  return f->nodes[d->definition[o.value]].op==RECEIVE;
}

/* 动态偏移保持原样；仅证明 store 的基址来自完整私有自动对象。
 * 不把未知指针、参数、全局对象或 volatile 内存当作私有 spill。 */
static bool
wide_ua_local_store(wide_function *f,wide_node *store,int at,
                    wide_address_dominance *d)
{
  if(store->op!=WM_STORE || store->width!=4 || store->byteAccess ||
     store->pointerAccess || store->codeAccess || store->addressSymbol ||
     store->addressOp || store->addressObject>=0 || store->addressIndex.value>=0 ||
     store->orderedReadCount || store->left.value<0)return false;
  int pointer=store->left.value;
  if(!wide_address_dominates(f,d,pointer,at))return false;
  wide_node *address=&f->nodes[d->definition[pointer]];
  if(address->op!='+' || address->width!=4 || address->left.value<0 ||
     address->right.sign || address->right.floating || address->right.codePointer ||
     !wide_affine_value(f,pointer,4,true))return false;
  int base=address->left.value;
  if(!wide_address_dominates(f,d,base,d->definition[pointer]))return false;
  wide_node *root=&f->nodes[d->definition[base]];
  if(root->op!=WM_ADDRESS || root->addressObject<0 || root->addressObject>=f->no ||
     root->addressOffset || root->addressOp || !root->addressSymbol)return false;
  symbol *s=f->objects[root->addressObject].sym;
  return s==root->addressSymbol && s->onStack && !s->_isparm &&
    !isVolatile(s->type) && !isAtomic(s->type) && !IS_EXTERN(s->etype);
}

/* 移动两个纯算术节点，所有真实节点身份和显式控制边经过完整映射。
 * 新初始化位于第一个标签之前；回边仍指向原标签，不再执行初始化。 */
static void
wide_ua_reorder(wide_function *f,int first,int multiply,int addition,int store)
{
  int mapped[WN_MAX],original[WN_MAX],out=0;
  wide_node *copy=Safe_alloc(f->nn*sizeof *copy);memcpy(copy,f->nodes,f->nn*sizeof *copy);
  for(int i=0;i<f->nn;++i) {
    if(i==first)original[out++]=multiply;
    if(i==multiply || i==addition)continue;
    original[out++]=i;
    if(i==store)original[out++]=addition;
  }
  wassert(out==f->nn);
  for(int i=0;i<f->nn;++i)mapped[original[i]]=i;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];*n=copy[original[i]];
    bool fall=n->op!=RETURN && n->op!=ENDFUNCTION && n->op!=GOTO &&
      n->op!=JUMPTABLE && n->succ[0]==original[i]+1;
    for(int k=0;k<2;++k)if(n->succ[k]>=0)
      n->succ[k]=(k==0 && fall)?i+1:mapped[n->succ[k]];
  }
  for(int i=0;i<f->nj;++i)f->jumpSuccessors[i]=mapped[f->jumpSuccessors[i]];
  Safe_free(copy);
}

static unsigned
wide_unsigned_affine_induction(wide_function *f)
{
  const char *enabled=getenv("MX251_UNSIGNED_AFFINE_INDUCTION");
  if(!enabled || strcmp(enabled,"1"))return 0;
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  for(int v=0;v<f->nv;++v)d->definition[v]=-1;
  for(int i=0;i<f->nn;++i) {
    if(f->nodes[i].op==FUNCTION) {if(d->entry>=0) {Safe_free(d);return 0;}d->entry=i;}
    for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].def,v)) {
      ++d->definitions[v];d->definition[v]=i;
    }
  }
  if(d->entry<0) {Safe_free(d);return 0;}
  wide_address_reachable(f,d->entry,-1,d->reachable);
  for(int latch=5;latch+1<f->nn;++latch) {
    wide_node *branch=&f->nodes[latch],*compare=&f->nodes[latch-1],*step=&f->nodes[latch-2];
    if(branch->op!=IFX || !branch->trueBranch || branch->succ[0]!=latch+1 ||
       compare->op!='<' || compare->result<0 || branch->left.value!=compare->result ||
       compare->width!=1 || compare->left.value<0 || compare->right.value>=0 ||
       step->op!='+' || step->result!=compare->left.value || step->left.value!=step->result ||
       (step->width!=1 && step->width!=2) ||
       !wide_ua_scalar(f,step->left,step->width) ||
       !wide_affine_literal(step->right,step->width,1) ||
       !wide_ua_scalar(f,compare->left,step->width) ||
       !wide_ua_scalar(f,compare->right,step->width) ||
       !wide_ua_uses(f,compare->result,latch))continue;
    int first=branch->succ[1],index=step->result;
    uint32_t bound=compare->right.literal,maximum=step->width==1?255u:65535u;
    if(first<1 || first>=latch-2 || f->nodes[first].op!=LABEL || !bound || bound>maximum)continue;
    while(first>0 && f->nodes[first-1].op==LABEL)--first;
    int init=first-1;
    if(init<0 || f->nodes[init].op!='=' || f->nodes[init].result!=index ||
       f->nodes[init].width!=step->width || !wide_affine_literal(f->nodes[init].right,step->width,0) ||
       !wide_affine_only_use_def(f,index,init,latch-2,-1,-1,-1,true) ||
       !d->reachable[init])continue;
    bool valid=true;int mul=-1,expr=-1,add=-1,write=-1;
    for(int i=first;i<latch;++i) {
      wide_node *n=&f->nodes[i];
      if(n->succ[0]!=i+1 || n->succ[1]>=0 || n->jumpCount ||
         n->byteAccess || n->orderedReadCount || n->orderedAsmCount)valid=false;
      switch(n->op) {
      case LABEL:case WM_NOP:case SEND:case '+':case WM_UMULW:case '<':break;
      case WM_UMUL32N:if(mul>=0)valid=false;mul=i;break;
      case WM_STORE:if(write>=0 || !wide_ua_local_store(f,n,i,d))valid=false;write=i;break;
      default:valid=false;break;
      }
    }
    if(f->nodes[init].succ[0]!=first || f->nodes[init].succ[1]>=0 || f->nodes[init].jumpCount)valid=false;
    for(int i=0;i<f->nn && valid;++i) {
      wide_node *n=&f->nodes[i];
      for(int k=0;k<2;++k) {
        int to=n->succ[k];if(to<first || to>latch)continue;
        if(!((i==init && k==0 && to==first) ||
             (i>=first && i<latch && k==0 && to==i+1) ||
             (i==latch && k==1 && to==branch->succ[1])))valid=false;
      }
      for(int k=0;k<n->jumpCount;++k) {
        int to=f->jumpSuccessors[n->jumpStart+k];if(to>=first && to<=latch)valid=false;
      }
    }
    if(!valid || mul<0 || write<0 || mul>=write)continue;
    wide_node *m=&f->nodes[mul];wide_operand factor=m->left;
    if(m->width!=4 || m->sign || m->floating || m->right.value<0 ||
       !wide_ua_invariant(f,factor,init,d) || !wide_ua_scalar(f,m->right,4) ||
       !wide_affine_value(f,m->result,4,false) || !wide_ua_definition(f,m->result,mul))continue;
    expr=d->definition[m->right.value];
    if(expr<=first || expr>=mul || !wide_ua_definition(f,m->right.value,expr) ||
       !wide_ua_uses(f,m->right.value,mul))continue;
    wide_node *x=&f->nodes[expr];
    if(x->op!='+' || x->width!=4 || x->sign || x->floating || x->left.value!=index ||
       !wide_ua_scalar(f,x->left,step->width) || !wide_affine_literal(x->right,4,1))continue;
    for(int i=mul+1;i<write;++i)if(wb_has(f->nodes[i].use,m->result)) {add=i;break;}
    if(add<0 || !wide_ua_uses(f,m->result,add))continue;
    wide_node *a=&f->nodes[add];wide_operand offset;
    if(a->op!='+' || a->width!=4 || a->sign || a->floating ||
       !wide_affine_value(f,a->result,4,false) || !wide_ua_definition(f,a->result,add) ||
       !wide_ua_uses(f,a->result,write) || f->nodes[write].right.value!=a->result)continue;
    if(a->left.value==m->result)offset=a->right;
    else if(a->right.value==m->result)offset=a->left;
    else continue;
    if(!wide_ua_invariant(f,offset,init,d))continue;
    int running=a->result,storeKey=f->nodes[write].origin->key,header=branch->target;
    /* unsigned 模 2^32：r(0)=a+b，r(k+1)=r(k)+b。
     * i 从 0 到 bound-1 且不回卷，所以每次写入的 r(k) 与原式相等。
     * 最后一次额外的更新结果没有任何消费者，原参数和计数器不变。 */
    m->op='+';m->left=offset;m->right=factor;m->result=running;
    m->origin=f->nodes[init].origin;m->use=m->def=(wide_bits){{0}};
    wb_set(&m->use,offset.value);wb_set(&m->use,factor.value);wb_set(&m->def,running);
    a->left=(wide_operand){.value=running,.width=4};a->right=factor;
    a->use=(wide_bits){{0}};wb_set(&a->use,running);wb_set(&a->use,factor.value);
    x->op=WM_NOP;x->result=x->left.value=x->right.value=-1;x->use=x->def=(wide_bits){{0}};
    wide_ua_reorder(f,first,mul,add,write);wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide unsigned-affine trial header=%d trip=%lu index=%d running=%d factor=%d offset=%d store-icode=%d modulo-bits=32 memory-order=unchanged\n",
      header,(unsigned long)bound,index,running,factor.value,offset.value,storeKey);
    Safe_free(d);return 1; /* 重排后不复用旧支配缓存。首版每函数至多一处。 */
  }
  Safe_free(d);return 0;
}
