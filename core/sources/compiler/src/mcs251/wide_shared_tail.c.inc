/* SPDX-License-Identifier: GPL-2.0-or-later
 * Factor isomorphic straight-line tails at a common join. Differing integer
 * literals become local merge values; memory operations stay in path order. */
enum { WST_NODES=32, WST_ARMS=6, WST_ARGS=4 };
typedef struct { int end,count,nodes[WST_NODES]; } wide_tail;

static bool
wide_tail_operation(const wide_node *n)
{
  if(n->args || n->stackArgs || n->jumpCount || n->orderedAsmCount ||
     n->orderedReadCount || n->memoryStateBarrier ||
     n->pointerAccess || n->nativeIndexed)return false;
  switch(n->op) {
  case '=':case CAST:case '+':case '-':case BITWISEAND:case '|':case '^':
  case LEFT_OP:case RIGHT_OP:case GETBYTE:case GETWORD:case GETABIT:
  case WM_LOAD:case WM_STORE:case WM_PARAM_LOAD:case WM_PARAM_STORE:
  case WM_OBJECT_LOAD:case WM_OBJECT_STORE:case WM_SYMBOL_LOAD:case WM_SYMBOL_STORE:
  case WM_ADDRESS:return true;
  default:return false;
  }
}

static wide_tail
wide_tail_collect(wide_function *f,int end,const int *predCount,const int *pred)
{
  wide_tail t={.end=end};int at=end;
  if(f->nodes[at].op==GOTO) {
    if(predCount[at]!=1 || pred[at]!=at-1)return t;
    --at;
  }
  while(at>=0 && t.count<WST_NODES) {
    wide_node *n=&f->nodes[at];
    if(predCount[at]!=1 || pred[at]!=at-1)break;
    if(n->succ[0]!=at+1 || n->succ[1]>=0 || n->jumpCount)break;
    if(n->op!=WM_NOP) {
      if(!wide_tail_operation(n))break;
      t.nodes[t.count++]=at;
    }
    if(predCount[at]!=1 || pred[at]!=at-1)break;
    --at;
  }
  return t; /* nodes are in reverse order, nearest the join first */
}

static bool
wide_tail_type(wide_operand a,wide_operand b)
{
  return a.width==b.width && a.sign==b.sign && a.floating==b.floating &&
    a.codePointer==b.codePointer;
}

static bool
wide_tail_parameter(const wide_node *n,int operand)
{
  if(operand==2)return n->addressOp!=0;
  if(n->op==WM_LOAD || n->op==WM_STORE)return operand==0 || n->op==WM_STORE;
  if(n->op=='=' || n->op==CAST || n->op==WM_SYMBOL_STORE ||
     n->op==WM_OBJECT_STORE || n->op==WM_PARAM_STORE)return operand==1;
  if(n->op=='+' || n->op=='-' || n->op==BITWISEAND || n->op=='|' || n->op=='^')return true;
  return false;
}

static bool
wide_tail_shape(wide_node a,wide_node b)
{
  a.origin=b.origin=NULL;
  a.left=b.left=a.right=b.right=a.addressIndex=b.addressIndex=(wide_operand){0};
  a.result=b.result=0;a.succ[0]=b.succ[0]=a.succ[1]=b.succ[1]=0;
  a.use=b.use=a.def=b.def=a.in=b.in=a.out=b.out=(wide_bits){{0}};
  return !memcmp(&a,&b,sizeof a);
}

static bool
wide_tail_match(wide_function *f,wide_tail *a,wide_tail *b,int length)
{
  int map[WV_MAX],reverse[WV_MAX];
  for(int v=0;v<f->nv;++v)map[v]=reverse[v]=-1;
  for(int k=length-1;k>=0;--k) {
    wide_node *x=&f->nodes[a->nodes[k]],*y=&f->nodes[b->nodes[k]];
    if(!wide_tail_shape(*x,*y))return false;
    wide_operand xo[3]={x->left,x->right,x->addressIndex},yo[3]={y->left,y->right,y->addressIndex};
    for(int o=0;o<3;++o) {
      if(!wide_tail_type(xo[o],yo[o]) || (xo[o].value<0)!=(yo[o].value<0))return false;
      if(xo[o].value>=0) {
        int other=map[yo[o].value];
        if((other>=0?other:yo[o].value)!=xo[o].value ||
           (reverse[xo[o].value]>=0 && reverse[xo[o].value]!=yo[o].value))return false;
      } else if(xo[o].literal!=yo[o].literal &&
          (!wide_tail_parameter(x,o) || xo[o].floating || xo[o].codePointer ||
           (xo[o].width!=1 && xo[o].width!=2 && xo[o].width!=4)))return false;
    }
    int av=x->result,bv=y->result;
    if((av<0)!=(bv<0))return false;
    if(av>=0) {
      wide_value *v=&f->values[av],*w=&f->values[bv];
      if(v->width!=w->width || v->sign!=w->sign || v->pointer!=w->pointer ||
         v->floating!=w->floating || v->codePointer!=w->codePointer ||
         v->nearData!=w->nearData || v->addressOnlyOffset!=w->addressOnlyOffset)return false;
      if((map[bv]>=0 && map[bv]!=av) || (reverse[av]>=0 && reverse[av]!=bv))return false;
      map[bv]=av;reverse[av]=bv;
      if(av!=bv)for(int i=0;i<f->nn;++i) {
        if((i<a->nodes[length-1] || i>a->nodes[0]) &&
           (wb_has(f->nodes[i].use,av) || wb_has(f->nodes[i].def,av)))return false;
        if((i<b->nodes[length-1] || i>b->nodes[0]) &&
           (wb_has(f->nodes[i].use,bv) || wb_has(f->nodes[i].def,bv)))return false;
      }
    }
  }
  return true;
}

static void
wide_tail_nop(wide_node *n,int next)
{
  iCode *origin=n->origin;
  *n=(wide_node){.op=WM_NOP,.origin=origin,.result=-1,.pairedResult=-1,
    .left.value=-1,.right.value=-1,.addressIndex.value=-1,.addressObject=-1,
    .label=-1,.target=-1,.succ={next,-1}};
}

static void
wide_tail_assignment(wide_node *n,int value,wide_operand literal)
{
  n->op='=';n->result=value;n->width=literal.width;n->sign=literal.sign;
  n->right=literal;wb_set(&n->def,value);
}

static bool
wide_tail_merge(wide_function *f,wide_tail arms[WST_ARMS],int count,int length)
{
  wide_operand constants[WST_ARGS][WST_ARMS];int param[WST_NODES][3],args=0;
  for(int k=0;k<length;++k)for(int o=0;o<3;++o) {
    wide_operand values[WST_ARMS];bool same=true;
    for(int a=0;a<count;++a) {
      wide_node *n=&f->nodes[arms[a].nodes[k]];
      values[a]=o==0?n->left:o==1?n->right:n->addressIndex;
      if(a && values[a].literal!=values[0].literal)same=false;
    }
    param[k][o]=-1;
    if(values[0].value>=0 || same)continue;
    int p=0;
    for(;p<args;++p) {
      bool equal=true;
      for(int a=0;a<count;++a)if(!wide_tail_type(values[a],constants[p][a]) ||
          values[a].literal!=constants[p][a].literal)equal=false;
      if(equal)break;
    }
    if(p==args) {
      if(args==WST_ARGS)return false;
      for(int a=0;a<count;++a)constants[args][a]=values[a];
      ++args;
    }
    param[k][o]=p;
  }
  /* Amortize parameter copies and branches over a substantial shared body. */
  if(!args || length<args+3 || f->nv+args>f->valueLimit || f->nn+args+1>f->nodeLimit)return false;
  int at=arms[0].nodes[length-1],label=0;
  for(int i=0;i<f->nn;++i)if(f->nodes[i].label>=label)label=f->nodes[i].label+1;
  int firstValue=f->nv;
  for(int p=0;p<args;++p) {
    wide_operand x=constants[p][0];
    f->values[f->nv++]=(wide_value){.sym=currFunc,.width=x.width,.sign=x.sign,
      .symbolSnapshot=true,.reg=-1,.slot=-1,.pin=-1,.preference=-1};
  }
  for(int k=0;k<length;++k) {
    wide_node *n=&f->nodes[arms[0].nodes[k]];
    for(int o=0;o<3;++o)if(param[k][o]>=0) {
      wide_operand *x=o==0?&n->left:o==1?&n->right:&n->addressIndex;
      x->value=firstValue+param[k][o];x->literal=0;wb_set(&n->use,x->value);
    }
  }
  for(int a=1;a<count;++a) {
    int start=arms[a].nodes[length-1];
    for(int i=start;i<=arms[a].end;++i)wide_tail_nop(&f->nodes[i],i+1);
    for(int p=0;p<args;++p)wide_tail_assignment(&f->nodes[start+p],firstValue+p,constants[p][a]);
    wide_node *jump=&f->nodes[start+args];jump->op=GOTO;jump->target=label;
    jump->succ[0]=at; /* patched to the new shared label after insertion */
  }
  int extra=args+1,oldCount=f->nn;
  for(int i=0;i<oldCount;++i)for(int k=0;k<2;++k)
    if(f->nodes[i].succ[k]>=at)f->nodes[i].succ[k]+=extra;
  for(int i=0;i<f->nj;++i)if(f->jumpSuccessors[i]>=at)f->jumpSuccessors[i]+=extra;
  memmove(&f->nodes[at+extra],&f->nodes[at],(oldCount-at)*sizeof f->nodes[0]);
  f->nn+=extra;
  for(int p=0;p<extra;++p) {
    f->nodes[at+p].origin=f->nodes[at+extra].origin;wide_tail_nop(&f->nodes[at+p],at+p+1);
    if(p<args)wide_tail_assignment(&f->nodes[at+p],firstValue+p,constants[p][0]);
    else {f->nodes[at+p].op=LABEL;f->nodes[at+p].label=label;}
  }
  f->nodes[at-1].succ[0]=at;
  for(int a=1;a<count;++a)f->nodes[arms[a].nodes[length-1]+args].succ[0]=at+args;
  dbuf_printf(f->diagnostics,"; wide shared-tail trial arms=%d nodes=%d parameters=%d label=%d\n",count,length,args,label);
  wide_reset_allocation(f);return true;
}

static unsigned
wide_shared_tails(wide_function *f)
{
  const char *option=getenv("MX251_SHARED_TAILS");
  if(option && !strcmp(option,"0"))return 0;
  /* Initial scope is acyclic MIR; loop-tail factoring needs its own path
   * accounting rather than paying for hot-path work with cold code savings. */
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    for(int k=0;k<2;++k)if(n->succ[k]>=0 && n->succ[k]<=i)return 0;
    if(n->jumpCount)return 0;
  }
  unsigned changed=0,budget=256;
  for(int round=0;round<4 && budget;++round) {
    int predCount[WN_MAX]={0},pred[WN_MAX];
    for(int i=0;i<f->nn;++i)for(int k=0;k<2;++k)if(f->nodes[i].succ[k]>=0) {
      int to=f->nodes[i].succ[k];++predCount[to];pred[to]=i;
    }
    bool merged=false;
    for(int join=f->nn-1;join>0 && !merged;--join) {
      if(f->nodes[join].op!=LABEL || predCount[join]<2)continue;
      wide_tail tails[WST_ARMS];int count=0;
      for(int i=join-1;i>=0 && count<WST_ARMS;--i)if(f->nodes[i].succ[0]==join && f->nodes[i].succ[1]<0) {
        wide_tail tail=wide_tail_collect(f,i,predCount,pred);
        if(tail.count>=4)tails[count++]=tail;
      }
      for(int anchor=0;anchor+1<count && !merged;++anchor)
        for(int length=tails[anchor].count;length>=4 && !merged && budget;--length) {
          wide_tail group[WST_ARMS];int matches=1;group[0]=tails[anchor];
          for(int other=anchor+1;other<count && budget;--budget,++other) {
            if(tails[other].count<length || tails[other].end>=group[0].nodes[length-1])continue;
            if(wide_tail_match(f,&group[0],&tails[other],length))group[matches++]=tails[other];
          }
          if(matches>=2 && wide_tail_merge(f,group,matches,length)) {++changed;merged=true;}
        }
    }
    if(!merged)break;
  }
  return changed;
}
