/* SPDX-License-Identifier: GPL-2.0-or-later
 * Collapse a diamond whose complete arms perform the same operations. Calls
 * remain on every original path, with the same arguments and memory order.
 * Loop back edges and jump-table entries are counted as incoming edges. */
enum { WEB_NODES=64 };

static bool
wide_equal_operation(const wide_node *n)
{
  if(n->jumpCount || n->orderedAsmCount || n->orderedReadCount ||
     n->memoryStateBarrier || n->nativeIndexed)return false;
  if(n->op==CALL)return true;
  return wide_tail_operation(n);
}

static bool
wide_equal_shape(wide_node a,wide_node b)
{
  if(a.op==CALL && b.op==CALL) {
    sym_link *x=wide_call_type(&a),*y=wide_call_type(&b);
    if(compareTypeExact(x,y,-1,true)!=1)return false;
    /* compareTypeExact's function branch does not compare parameter types. */
    value *p=FUNC_ARGS(x),*q=FUNC_ARGS(y);
    for(;p && q;p=p->next,q=q->next)
      if(compareTypeExact(p->type,q->type,-1,true)!=1 ||
         getSize(p->type)!=getSize(q->type))return false;
    if(p || q)return false;
  }
  memset(a.stackOperands,0,sizeof a.stackOperands);
  memset(b.stackOperands,0,sizeof b.stackOperands);
  a.call=b.call=(wide_call_contract){0};
  a.callFrameValues=b.callFrameValues=a.callStores=b.callStores=
    a.callLoads=b.callLoads=a.callArguments=b.callArguments=(wide_bits){{0}};
  return wide_tail_shape(a,b);
}

static bool
wide_equal_arms(wide_function *f,const int *a,const int *b,int count,
                int startA,int endA,int startB,int endB)
{
  int map[WV_MAX],reverse[WV_MAX];
  for(int v=0;v<f->nv;++v)map[v]=reverse[v]=-1;
  for(int k=0;k<count;++k) {
    wide_node *x=&f->nodes[a[k]],*y=&f->nodes[b[k]];
    if(!wide_equal_shape(*x,*y))return false;
    wide_operand xo[WA_MAX+3]={x->left,x->right,x->addressIndex};
    wide_operand yo[WA_MAX+3]={y->left,y->right,y->addressIndex};
    for(unsigned o=0;o<x->stackArgs;++o) {
      xo[o+3]=x->stackOperands[o];yo[o+3]=y->stackOperands[o];
    }
    for(unsigned o=0;o<3+x->stackArgs;++o) {
      if(!wide_tail_type(xo[o],yo[o]) || (xo[o].value<0)!=(yo[o].value<0))return false;
      if(xo[o].value<0) {if(xo[o].literal!=yo[o].literal)return false;}
      else {
        int other=map[yo[o].value];
        if((other>=0?other:yo[o].value)!=xo[o].value ||
           (reverse[xo[o].value]>=0 && reverse[xo[o].value]!=yo[o].value))return false;
      }
    }
    int av=x->result,bv=y->result;
    if((av<0)!=(bv<0))return false;
    if(av<0)continue;
    wide_value *v=&f->values[av],*w=&f->values[bv];
    if(v->width!=w->width || v->sign!=w->sign || v->pointer!=w->pointer ||
       v->floating!=w->floating || v->codePointer!=w->codePointer ||
       v->nearData!=w->nearData || v->addressOnlyOffset!=w->addressOnlyOffset ||
       v->pin!=w->pin)return false;
    if((map[bv]>=0 && map[bv]!=av) || (reverse[av]>=0 && reverse[av]!=bv))return false;
    map[bv]=av;reverse[av]=bv;
    /* A renamed temporary belongs entirely to its own arm. Values observed
     * at the join (including loop-carried results) must retain their ID. */
    if(av!=bv)for(int i=0;i<f->nn;++i) {
      if((i<startA || i>=endA) &&
         (wb_has(f->nodes[i].use,av) || wb_has(f->nodes[i].def,av)))return false;
      if((i<startB || i>=endB) &&
         (wb_has(f->nodes[i].use,bv) || wb_has(f->nodes[i].def,bv)))return false;
    }
  }
  return true;
}

static void
wide_equal_dead_condition(wide_function *f,int value)
{
  wide_bits candidates={{0}};wb_set(&candidates,value);bool changed;
  do {
    changed=false;wide_bits used={{0}};
    for(int i=0;i<f->nn;++i)used=wb_or(used,f->nodes[i].use);
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(!wb_has(candidates,n->result) || wb_has(used,n->result) ||
         n->byteAccess || n->orderedReadCount || n->orderedAsmCount ||
         n->memoryStateBarrier || n->floating)continue;
      switch(n->op) {
      case '=':case CAST:case '+':case '-':case BITWISEAND:case '|':case '^':
      case '!':case '~':case LEFT_OP:case RIGHT_OP:case GETBYTE:case GETWORD:
      case GETABIT:case '<':case '>':case LE_OP:case GE_OP:case EQ_OP:case NE_OP:
      case WM_ADDRESS:case WM_BIT_CAST:
        candidates=wb_or(candidates,n->use);wide_tail_nop(n,i+1);changed=true;break;
      default:break; /* In particular, never delete a condition's memory read. */
      }
    }
  } while(changed);
}

static unsigned
wide_equal_branches(wide_function *f)
{
  const char *option=getenv("MX251_EQUAL_BRANCHES");
  if(option && !strcmp(option,"0"))return 0;
  unsigned changed=0;
  /* Inner diamonds first; another sweep can then expose an equal outer pair. */
  for(int round=0;round<4;++round) {
    int predecessors[WN_MAX]={0};bool merged=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      for(int k=0;k<2+n->jumpCount;++k) {
        int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
        if(to>=0)++predecessors[to];
      }
    }
    for(int i=f->nn-1;i>=0;--i) {
      wide_node *branch=&f->nodes[i];
      if(branch->op!=IFX || branch->succ[0]!=i+1 || branch->orderedReadCount ||
         branch->memoryStateBarrier)continue;
      int other=branch->succ[1];
      if(other<=i+1 || other>=f->nn || f->nodes[other].op!=LABEL)continue;
      int last=other-1,join=f->nodes[last].succ[0];
      if(f->nodes[last].op!=GOTO || join<=other || join>=f->nn ||
         f->nodes[join].op!=LABEL)continue;
      int nodes[2][WEB_NODES],counts[2]={0};bool valid=true;
      for(int arm=0;arm<2 && valid;++arm) {
        int begin=arm?other:i+1,end=arm?join:last;
        for(int at=begin;at<end;++at) {
          wide_node *n=&f->nodes[at];
          if(predecessors[at]!=1 || n->succ[0]!=at+1 || n->succ[1]>=0 || n->jumpCount) {valid=false;break;}
          if(n->op==WM_NOP || n->op==LABEL || n->op==SEND)continue;
          if(counts[arm]==WEB_NODES || !wide_equal_operation(n)) {valid=false;break;}
          nodes[arm][counts[arm]++]=at;
        }
      }
      if(!valid || predecessors[last]!=1 || f->nodes[last].succ[1]>=0 ||
         counts[0]!=counts[1] || !counts[0] ||
         !wide_equal_arms(f,nodes[0],nodes[1],counts[0],i+1,last,other,join))continue;
      int condition=branch->left.value;
      wide_tail_nop(branch,i+1);
      for(int at=last;at<join;++at)wide_tail_nop(&f->nodes[at],at+1);
      wide_equal_dead_condition(f,condition);
      dbuf_printf(f->diagnostics,"; wide equal-branch trial branch=%d nodes=%d join=%d\n",i,counts[0],join);
      ++changed;merged=true;break; /* Rebuild incoming counts after each edit. */
    }
    if(!merged)break;
  }
  if(changed)wide_reset_allocation(f);
  return changed;
}
