/* SPDX-License-Identifier: GPL-2.0-or-later
 * 非逃逸 MIR 标量常量初始化的有限下沉。仅移动原节点，不复制或缓存内存。
 * 目标是只执行一次的循环入口边；回边直接到原 LABEL，不能重复初始化。
 * 本规则置于 range 试算内部：未消除的 CALL/未知观察者严格拒绝。
 */
static bool
wide_loop_initializer_enabled(void)
{
  const char *p=getenv("MX251_LOOP_INITIALIZER_SINK");
  return p && !strcmp(p,"1");
}

static bool
wide_initializer_reach(wide_function *f,int start,int omit,int edgeFrom,int edgeTo,
                       unsigned char seen[WN_MAX])
{
  int queue[WN_MAX],head=0,tail=0;memset(seen,0,WN_MAX);
  if(start<0 || start>=f->nn || start==omit)return false;
  seen[start]=1;queue[tail++]=start;
  while(head<tail) {
    int at=queue[head++];wide_node *n=&f->nodes[at];
    for(int k=0;k<2+n->jumpCount;++k) {
      int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
      if(to<0)continue;
      if(to>=f->nn)return false;
      if(to==omit || (at==edgeFrom && to==edgeTo) || seen[to])continue;
      if(tail>=WN_MAX)return false;seen[to]=1;queue[tail++]=to;
    }
  }
  return true;
}

/* 普通 MIR 临时 spill 是编译器私有表示，不是 C 对象。仍保守排除外部
 * 调用、特殊状态、全局/参数内存及未知指针观察者。局部数组的实际读写
 * 全部保留；这里只追完整对象根，不据此移动/合并读写或推断 p/q 不别名。 */
static bool
wide_initializer_observers(wide_function *f,const char **why)
{
  int definitions[WV_MAX]={0},root[WV_MAX],returns=0;
  for(int v=0;v<f->nv;++v)root[v]=-1;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    for(int v=0;v<f->nv;++v)definitions[v]+=wb_has(n->def,v);
    if(n->op==RETURN)++returns;
    if(n->op==CALL || n->op==PCALL || n->op==WM_ORDERED_ASM ||
       n->op==WM_VOLATILE_BIT_READ || n->op==WM_BIT_LOAD || n->op==WM_BIT_STORE ||
       n->op==WM_SYMBOL_LOAD || n->op==WM_SYMBOL_STORE || n->op==WM_PARAM_STORE ||
       n->op==WM_PARAM_ADDRESS || n->byteAccess || n->codeAccess || n->pointerAccess) {
      *why="call-or-observable-state";return false;
    }
  }
  if(returns!=1){*why="multiple-or-missing-return";return false;}
  bool changed;
  do {
    changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];int v=n->result,o=-1,source=-1;
      if(v<0 || root[v]>=0 || !f->values[v].pointer || definitions[v]!=1)continue;
      if(n->op==WM_ADDRESS && n->addressObject>=0 && !n->addressOp) {
        o=n->addressObject;
        if(o>=f->no || n->addressSymbol!=f->objects[o].sym ||
           isVolatile(f->objects[o].sym->type) || isAtomic(f->objects[o].sym->type))continue;
      } else if((n->op=='=' || n->op==CAST) && n->right.value>=0 &&
                 n->right.width==4 && f->values[n->right.value].pointer)source=n->right.value;
      else if((n->op=='+' || n->op=='-') && n->left.value>=0 &&
              f->values[n->left.value].pointer && !n->right.floating &&
              (n->right.value<0 || !f->values[n->right.value].pointer))source=n->left.value;
      if(source>=0)o=root[source];
      if(o>=0){root[v]=o;changed=true;}
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==WM_LOAD || n->op==WM_STORE) {
      int o=n->addressObject;
      if(o<0 && n->left.value>=0)o=root[n->left.value];
      if(o<0 || o>=f->no){*why="unknown-memory-observer";return false;}
    }
    for(int v=0;v<f->nv;++v)if(f->values[v].pointer && wb_has(n->use,v)) {
      bool access=(n->op==WM_LOAD || n->op==WM_STORE) && n->left.value==v &&
        n->right.value!=v && n->addressIndex.value!=v;
      bool derive=n->result>=0 && f->values[n->result].pointer &&
        (((n->op=='=' || n->op==CAST) && n->right.value==v) ||
         ((n->op=='+' || n->op=='-') && n->left.value==v && n->right.value!=v));
      if(root[v]<0 || (!access && !derive)){*why="escaped-or-observed-pointer";return false;}
    }
  }
  return true;
}

static bool
wide_initializer_scalar(wide_function *f,wide_node *n)
{
  int v=n->result;
  if(n->op!='=' || v<0 || v>=f->nv || !n->origin || n->right.value>=0 ||
     n->left.value>=0 || n->addressIndex.value>=0 || n->right.floating ||
     n->floating || n->right.codePointer || n->width!=n->right.width ||
     (n->width!=1 && n->width!=2 && n->width!=4) || n->args || n->stackArgs ||
     n->addressOp || n->addressObject>=0 || n->byteAccess || n->codeAccess ||
     n->pointerAccess || !wb_empty(n->use))return false;
  wide_value *value=&f->values[v];operand *result=IC_RESULT(n->origin);
  if(!result || !IS_ITEMP(result) || IS_OP_GLOBAL(result) ||
     OP_SYMBOL(result)!=value->sym || value->sym->addrtaken ||
     value->width!=n->width || value->pointer || value->codePointer ||
     value->floating || value->symbolSnapshot || value->pin>=0 ||
     isVolatile(value->sym->type) || isAtomic(value->sym->type))return false;
  wide_bits only={{0}};wb_set(&only,v);
  return wb_equal(n->def,only);
}

static unsigned
wide_sink_loop_initializers(wide_function *f)
{
  if(!wide_loop_initializer_enabled())return 0;
  const char *why="no-one-shot-entry";int entry=-1;unsigned moved=0,budget=256;
  if(!wide_initializer_observers(f,&why)) {
    dbuf_printf(f->diagnostics,"; wide loop-initializer reject reason=%s\n",why);return 0;
  }
  for(int i=0;i<f->nn;++i)if(f->nodes[i].op==FUNCTION) {
    if(entry>=0)return 0;entry=i;
  }
  if(entry<0)return 0;
  unsigned char reachable[WN_MAX],withoutDef[WN_MAX],fromHeader[WN_MAX],withoutEdge[WN_MAX],afterDef[WN_MAX];
  if(!wide_initializer_reach(f,entry,-1,-1,-1,reachable))return 0;
  /* 每个临时值最多移动一次，图搜索总数有界；预算用尽即保留其余原节点。 */
  for(int v=0;v<f->nv && budget>3;++v) {
    int definition=-1;unsigned touches=0;
    for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].def,v) &&
       wide_initializer_scalar(f,&f->nodes[i])) {
      if(definition>=0){definition=-2;break;}definition=i;
    }
    if(definition<0 || !reachable[definition] ||
       f->nodes[definition].succ[0]!=definition+1 || f->nodes[definition].succ[1]>=0 ||
       f->nodes[definition].jumpCount)continue;
    budget-=2;
    if(!wide_initializer_reach(f,entry,definition,-1,-1,withoutDef) ||
       !wide_initializer_reach(f,definition+1,-1,-1,-1,afterDef) || afterDef[definition])continue;
    bool valid=true;
    for(int i=0;i<f->nn && valid;++i) {
      wide_node *n=&f->nodes[i];
      if(i!=definition && (wb_has(n->use,v) || wb_has(n->def,v))) {
        ++touches;if(!reachable[i] || withoutDef[i])valid=false;
      }
      for(int k=0;k<2+n->jumpCount;++k) {
        int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
        if(to==definition && !(i==definition-1 && k==0 && wide_sink_fallthrough(n,i)))valid=false;
      }
    }
    if(!valid || touches<2)continue;
    for(int h=f->nn-1;h>definition+2 && budget>1;--h) {
      if(f->nodes[h].op!=LABEL || !reachable[h])continue;
      int pred=h-1;wide_node *p=&f->nodes[pred];
      if(!wide_sink_fallthrough(p,pred) || p->succ[1]>=0 || p->jumpCount ||
         withoutDef[pred])continue;
      budget-=2;
      if(!wide_initializer_reach(f,h,-1,-1,-1,fromHeader) || fromHeader[pred] ||
         !wide_initializer_reach(f,entry,-1,pred,h,withoutEdge))continue;
      bool back=false;valid=true;
      for(int i=0;i<f->nn && valid;++i) {
        wide_node *n=&f->nodes[i];
        /* 全部后续定义同样须受新入口边支配；不能覆盖另一路先写的新值。 */
        if(i!=definition && (wb_has(n->use,v) || wb_has(n->def,v)) && withoutEdge[i])valid=false;
        for(int k=0;k<2+n->jumpCount;++k) {
          int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
          if(to!=h || (i==pred && k==0))continue;
          if(fromHeader[i])back=true;else valid=false;
        }
      }
      if(!valid || !back)continue;
      int key=f->nodes[definition].origin->key,headerKey=f->nodes[h].origin?f->nodes[h].origin->key:-1;
      wide_sink_move_before(f,definition,h);++moved;
      dbuf_printf(f->diagnostics,"; wide loop-initializer moved value=%d key=%d from=%d before=%d header-key=%d touches=%u proof=all-def-use-edge-dominance-one-shot-local-observers\n",v,key,definition,h,headerKey,touches);
      /* 后续候选基于已经变化的图重新证明，而非复用旧下标的死点。 */
      if(!wide_initializer_reach(f,entry,-1,-1,-1,reachable))return 0;
      break;
    }
  }
  if(moved)wide_reset_allocation(f);
  return moved;
}
