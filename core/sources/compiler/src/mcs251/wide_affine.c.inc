/* 有界字节数组循环的地址递推试算。只移动纯地址计算；原字节事务、
 * index 更新和最终比较完全保留。所有未知形态仍走完整旧候选。 */
static bool
wide_affine_literal(wide_operand x,unsigned width,uint32_t literal)
{
  return x.value<0 && x.width==(int)width && !x.sign && !x.floating &&
    !x.codePointer && x.literal==literal;
}

static bool
wide_affine_value(wide_function *f,int value,int width,bool pointer)
{
  if(value<0 || value>=f->nv)return false;
  wide_value *v=&f->values[value];
  return v->width==width && v->pointer==pointer && !v->sign && !v->floating &&
    !v->codePointer && !v->symbolSnapshot && v->sym && !v->sym->addrtaken &&
    !isVolatile(v->sym->type) && !isAtomic(v->sym->type);
}

static bool
wide_affine_only_use_def(wide_function *f,int value,int def1,int def2,
                        int use1,int use2,int use3,bool outsideUses)
{
  for(int i=0;i<f->nn;++i) {
    if(wb_has(f->nodes[i].def,value) && i!=def1 && i!=def2)return false;
    if(!outsideUses && wb_has(f->nodes[i].use,value) && i!=use1 && i!=use2 && i!=use3)return false;
  }
  return true;
}

/* 按真实节点身份重排，而不是重写 label 字符串。普通直落边跟随新顺序；
 * IFX/GOTO/表跳转保留原目标身份。完整数组映射防止其它区域边遗漏。 */
static void
wide_affine_reorder(wide_function *f,int firstLabel,int address,int addition,int store)
{
  int mapped[WN_MAX],original[WN_MAX];
  for(int i=0;i<f->nn;++i)mapped[i]=i;
  mapped[address]=firstLabel;
  for(int i=firstLabel;i<address;++i)mapped[i]=i+1;
  mapped[store]=addition;mapped[addition]=store;
  for(int i=0;i<f->nn;++i)original[mapped[i]]=i;
  wide_node base=f->nodes[address],add=f->nodes[addition],write=f->nodes[store];
  memmove(&f->nodes[firstLabel+1],&f->nodes[firstLabel],(address-firstLabel)*sizeof f->nodes[0]);
  f->nodes[firstLabel]=base;f->nodes[addition]=write;f->nodes[store]=add;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    bool fall=n->op!=RETURN && n->op!=ENDFUNCTION && n->op!=GOTO &&
      n->op!=JUMPTABLE && n->succ[0]==original[i]+1;
    for(int k=0;k<2;++k)if(n->succ[k]>=0)
      n->succ[k]=(k==0 && fall)?i+1:mapped[n->succ[k]];
  }
  for(int i=0;i<f->nj;++i)f->jumpSuccessors[i]=mapped[f->jumpSuccessors[i]];
}

static unsigned
wide_affine_byte_loops(wide_function *f)
{
  const char *enabled=getenv("MX251_AFFINE_BYTE_LOOP");
  if(!enabled || strcmp(enabled,"1"))return 0;
  unsigned changed=0;
  for(int latch=7;latch+1<f->nn;++latch) {
    wide_node *branch=&f->nodes[latch],*compare=&f->nodes[latch-1],*step=&f->nodes[latch-2];
    int store=latch-3,addition=latch-4,address=latch-5;
    wide_node *write=&f->nodes[store],*add=&f->nodes[addition],*base=&f->nodes[address];
    if(branch->op!=IFX || !branch->trueBranch || branch->succ[0]!=latch+1 ||
       compare->op!='<' || compare->result<0 || branch->left.value!=compare->result ||
       compare->left.value<0 || compare->right.value>=0 || compare->left.sign ||
       compare->right.sign || compare->left.floating || compare->right.floating ||
       compare->left.width!=compare->right.width || compare->width!=1 ||
       step->op!='+' || step->result!=compare->left.value || step->left.value!=step->result ||
       (step->width!=1 && step->width!=2) || step->left.width!=step->width ||
       compare->left.width!=step->width || !wide_affine_literal(step->right,step->width,1) ||
       base->op!=WM_ADDRESS || base->addressObject>=0 || base->addressOffset || base->addressOp ||
       !base->addressSymbol || add->op!='+' || add->left.value!=base->result ||
       add->right.value!=step->result || add->right.width!=step->width ||
       add->width!=4 || add->left.width!=4 || add->right.sign ||
       write->op!=WM_STORE || write->left.value!=add->result || write->width!=1 ||
       write->left.width!=4 || write->right.value>=0 || write->right.width!=1 ||
       write->right.floating || write->right.literal>255 || write->codeAccess ||
       write->pointerAccess || write->addressObject>=0 || write->addressSymbol || write->addressOp)continue;
    int index=step->result,pointer=add->result,baseValue=base->result;
    if(!wide_affine_value(f,index,step->width,false) ||
       !wide_affine_value(f,pointer,4,true) || !wide_affine_value(f,baseValue,4,true) ||
       index==pointer || pointer==baseValue || baseValue==index)continue;
    uint32_t bound=compare->right.literal,maximum=step->width==1?255u:65535u;
    symbol *array=base->addressSymbol;memmap *space=SPEC_OCLS(array->etype);
    if(!bound || bound>maximum || !IS_ARRAY(array->type) ||
       !IS_SPEC(array->type->next) || !IS_INTEGRAL(array->type->next) ||
       getSize(array->type->next)!=1 || IS_BITVAR(array->type->next) ||
       isAtomic(array->type) || isAtomic(array->type->next) ||
       array->onStack || array->_isparm || IS_EXTERN(array->etype) || SPEC_ABSA(array->etype) ||
       !IN_FARSPACE(space) || space->paged || IN_CODESPACE(space) ||
       getSize(array->type)<=0 || bound>(unsigned)getSize(array->type))continue;
    int first=address;
    while(first>0 && f->nodes[first-1].op==LABEL)--first;
    int init=first-1;
    if(first==address || init<0 || branch->succ[1]<first || branch->succ[1]>=address ||
       f->nodes[init].op!='=' || f->nodes[init].result!=index ||
       f->nodes[init].width!=step->width || !wide_affine_literal(f->nodes[init].right,step->width,0))continue;
    bool valid=true;
    /* 首版严格直线循环：无 body 外入、第二回边、隐式跳表边或提前出口。 */
    for(int i=first;i<latch;++i)if(f->nodes[i].succ[0]!=i+1 ||
       f->nodes[i].succ[1]>=0 || f->nodes[i].jumpCount)valid=false;
    if(f->nodes[init].succ[0]!=first || f->nodes[init].succ[1]>=0 || f->nodes[init].jumpCount)valid=false;
    for(int i=0;i<f->nn && valid;++i) {
      wide_node *n=&f->nodes[i];
      for(int k=0;k<2;++k) {
        int to=n->succ[k];
        if(to<first || to>latch)continue;
        if(!((i==init && k==0 && to==first) ||
             (i>=first && i<latch && k==0 && to==i+1) ||
             (i==latch && k==1 && to==branch->succ[1])))valid=false;
      }
      for(int k=0;k<n->jumpCount;++k) {
        int to=f->jumpSuccessors[n->jumpStart+k];
        if(to>=first && to<=latch)valid=false;
      }
    }
    if(!valid || !wide_affine_only_use_def(f,index,init,latch-2,addition,latch-2,latch-1,true) ||
       !wide_affine_only_use_def(f,baseValue,address,-1,addition,-1,-1,false) ||
       !wide_affine_only_use_def(f,pointer,addition,-1,store,-1,-1,false) ||
       !wide_affine_only_use_def(f,compare->result,latch-1,-1,latch,-1,-1,false))continue;
    /* 退出地址从末元素变为 one-past，只在原地址/基址均无其它消费者时允许。
     * pointer 仍由既有宽加法发射，每次保留 modulo-2^24 规范化。 */
    int storeKey=write->origin->key;
    base->result=pointer;base->def=(wide_bits){{0}};wb_set(&base->def,pointer);
    add->left=(wide_operand){.value=pointer,.width=4};
    add->right=(wide_operand){.value=-1,.width=4,.literal=1};
    add->use=(wide_bits){{0}};wb_set(&add->use,pointer);
    wide_affine_reorder(f,first,address,addition,store);
    dbuf_printf(f->diagnostics,"; wide affine-byte-loop trial header=%d trip=%lu array=%s object-bytes=%d index=%d pointer=%d store-icode=%d transaction-bytes=1 modulo-bits=24 index-preserved=1\n",
      branch->target,(unsigned long)bound,array->rname,getSize(array->type),index,pointer,storeKey);
    ++changed;
  }
  if(changed)wide_reset_allocation(f);
  return changed;
}
