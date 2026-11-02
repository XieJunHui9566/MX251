/* GPL-2.0-or-later. Bounded closed-parameter switch specialization.
 * No source/function/device names are policy. No pointee or volatile caching. */
typedef struct {
  int receive, parameter, compare, branch, jump, compareValue;
  unsigned count, values[4]; int destinations[4];
} wide_domain_shape;

static bool
wide_domain_shape_check(wide_function *f,wide_domain_shape *s)
{
  memset(s,0,sizeof *s);s->receive=s->jump=-1;
  s->count=f->domainCount;memcpy(s->values,f->domainValues,sizeof s->values);
  if(s->count<2 || s->count>4 || f->ordinaryEntry || f->no || f->formalAddress ||
     !IS_VOID(currFunc->type->next))return false;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==RECEIVE && n->origin->argreg==1) {
      if(s->receive>=0 || n->result<0 || n->width!=1 || n->sign || n->floating)return false;
      s->receive=i;s->parameter=n->result;
    }
    if(n->op==JUMPTABLE){if(s->jump>=0)return false;s->jump=i;}
    if(n->op==CALL || n->op==WM_ORDERED_ASM || n->op==WM_PARAM_STORE ||
       n->op==WM_PARAM_ADDRESS)return false;
    /* 首版无循环和二次入口：所有实际边严格向前，不能只看文本顺序。 */
    for(int k=0;k<2;++k)if(n->succ[k]>=0 && n->succ[k]<=i)return false;
    for(int k=0;k<n->jumpCount;++k)
      if(f->jumpSuccessors[n->jumpStart+k]<=i)return false;
  }
  if(s->receive<0 || s->jump<3)return false;
  s->compare=s->jump-2;s->branch=s->jump-1;
  wide_node *g=&f->nodes[s->compare],*b=&f->nodes[s->branch],*j=&f->nodes[s->jump];
  /* 只证明未经最小值偏移的 unsigned byte 索引，且整个域均在原表内。
   * 归一化、默认域、隐式窄化或修改后的参数均保留原实现。 */
  if(g->op!='>' || g->result<0 || g->left.value!=s->parameter ||
     g->left.width!=1 || g->left.sign || g->left.floating ||
     g->right.value>=0 || g->right.width!=1 || g->right.sign || g->right.floating ||
     g->right.literal!=(unsigned)j->jumpCount-1 || b->op!=IFX || !b->trueBranch ||
     b->left.value!=g->result || b->succ[0]!=s->jump || b->succ[1]<=s->jump ||
     j->left.value!=s->parameter || j->left.width!=1 || j->left.sign ||
     j->jumpCount<=0 || j->jumpCount>256)return false;
  s->compareValue=g->result;
  for(unsigned k=0;k<s->count;++k) {
    if(s->values[k]>=(unsigned)j->jumpCount)return false;
    s->destinations[k]=f->jumpSuccessors[j->jumpStart+s->values[k]];
  }
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(wb_has(n->def,s->parameter) && i!=s->receive)return false;
    if(wb_has(n->def,s->compareValue) && i!=s->compare)return false;
    if(wb_has(n->use,s->compareValue) && i!=s->branch)return false;
    if(n->op==IFX && i!=s->branch)return false;
    if(i<s->compare && n->op!=FUNCTION && n->op!=LABEL && n->op!=RECEIVE && n->op!=WM_NOP)return false;
    if(i<s->compare && (n->succ[0]!=i+1 || n->succ[1]>=0))return false;
  }
  return true;
}

static bool
wide_domain_transform(wide_function *f,wide_domain_shape *s)
{
  bool keep[WN_MAX]={false};int map[WN_MAX];
  for(int i=0;i<s->compare;++i)keep[i]=true;
  for(unsigned k=0;k<s->count;++k) {
    int at=s->destinations[k],steps=0;
    while(at>=0) {
      if(at<=s->jump || at>=f->nn || ++steps>f->nn)return false;
      keep[at]=true;wide_node *n=&f->nodes[at];
      if(n->op==RETURN || n->op==ENDFUNCTION)break;
      if(n->succ[0]<0 || n->succ[1]>=0 || n->jumpCount)return false;
      at=n->succ[0];
    }
  }
  int added=2*(int)(s->count-1)+1,number=s->compare+added;
  for(int i=0;i<f->nn;++i)map[i]=-1;
  for(int i=0;i<s->compare;++i)map[i]=i;
  for(int i=s->jump+1;i<f->nn;++i)if(keep[i])map[i]=number++;
  if(number>f->nodeLimit)return false;
  wide_node *nodes=Safe_alloc(sizeof f->nodes);
  for(int i=0;i<s->compare;++i)nodes[i]=f->nodes[i];
  int at=s->compare;
  for(unsigned k=0;k<s->count-1;++k) {
    wide_node c=f->nodes[s->compare],b=f->nodes[s->branch];
    c.op=EQ_OP;c.right.literal=s->values[k];c.succ[0]=at+1;c.succ[1]=-1;
    b.target=f->nodes[s->destinations[k]].label;b.trueBranch=true;
    b.succ[0]=at+2;b.succ[1]=map[s->destinations[k]];
    nodes[at++]=c;nodes[at++]=b;
  }
  wide_node last=f->nodes[s->branch];last.op=GOTO;
  last.target=f->nodes[s->destinations[s->count-1]].label;
  last.succ[0]=map[s->destinations[s->count-1]];last.succ[1]=-1;
  last.left.value=-1;last.use=last.def=(wide_bits){{0}};nodes[at++]=last;
  for(int i=s->jump+1;i<f->nn;++i)if(keep[i]) {
    wide_node n=f->nodes[i];
    for(int k=0;k<2;++k)if(n.succ[k]>=0) {
      if(map[n.succ[k]]<0){Safe_free(nodes);return false;}
      n.succ[k]=map[n.succ[k]];
    }
    nodes[at++]=n;
  }
  if(at!=number){Safe_free(nodes);return false;}
  memcpy(f->nodes,nodes,number*sizeof *nodes);Safe_free(nodes);
  f->nn=number;f->nj=0;
  for(unsigned k=0;k<s->count;++k)s->destinations[k]=map[s->destinations[k]];
  s->jump=s->compare+added-1;
  wide_reset_allocation(f);wide_liveness(f);return true;
}

enum { WDM_INS,WDM_MOV,WDM_MEMORY,WDM_STACK_READ,WDM_STACK_WRITE,WDM_PUSH,WDM_POP,WDM_BYTES,WDM_COUNT };
typedef struct { unsigned m[WDM_COUNT]; } wide_domain_metric;
static void wd_add(wide_domain_metric *a,const wide_domain_metric *b)
{for(unsigned k=0;k<WDM_COUNT;++k)a->m[k]+=b->m[k];}

/* 只对实际发射文本计数。节点边界是编译器注释，不改变机器代码；
 * 地址/编码任一未知，内部条件分支或缺少对应节点都拒绝比较。
 * 跳表节点仅将实际执行的一项 EJMP 计入，而不是把全部表项相加。 */
static bool
wide_domain_emitted_metrics(wide_function *f,const char *text,size_t length,
                           wide_domain_shape *s,bool original,wide_domain_metric *metrics,
                           wide_domain_metric *prefix)
{
  int current=-1,previous=-1;bool table=false;unsigned entries=0;bool marker=false;
  memset(metrics,0,WN_MAX*sizeof *metrics);memset(prefix,0,sizeof *prefix);
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    const char *first=text+at,*last=text+end;at=end+1;
    while(first<last && isspace((unsigned char)*first))++first;
    if((size_t)(last-first)>=21 && !strncmp(first,"; mx251-domain-node=",20)) {
      char *tail;long index=strtol(first+20,&tail,10);
      if(index<0 || index>=f->nn || index<=previous || tail!=last)return false;
      current=previous=(int)index;table=false;marker=true;continue;
    }
    if((size_t)(last-first)==21 && !memcmp(first,"; mx251-domain-table",20))return false;
    if((size_t)(last-first)==20 && !memcmp(first,"; mx251-domain-table",20)) {
      if(!original || current!=s->jump || table)return false;table=true;continue;
    }
    const char *comment=memchr(first,';',(size_t)(last-first));if(comment)last=comment;
    while(last>first && isspace((unsigned char)last[-1]))--last;
    if(first==last || last[-1]==':')continue;
    unsigned bytes=wide_instruction_bytes_context(first,(size_t)(last-first),wide_resolve_cost_operand,f);
    if(!bytes)return false;
    const char *space=first;while(space<last && !isspace((unsigned char)*space))++space;
    char op[20];size_t opLength=space-first;if(!opLength || opLength>=sizeof op)return false;
    memcpy(op,first,opLength);op[opLength]=0;
    if(wlc_conditional(op) && (current<s->compare || current>s->jump))return false;
    if(!strcmp(op,"ecall"))return false;
    if(table) {if(strcmp(op,"ejmp"))return false;if(++entries>1)continue;}
    wide_domain_metric m={{0}};m.m[WDM_INS]=1;m.m[WDM_BYTES]=bytes;
    m.m[WDM_MOV]=!strcmp(op,"mov");m.m[WDM_MEMORY]=memchr(space,'@',(size_t)(last-space))!=NULL && strcmp(op,"ejmp");
    m.m[WDM_PUSH]=!strcmp(op,"push");m.m[WDM_POP]=!strcmp(op,"pop");
    const char *stack=NULL;
    for(const char *p=space;p+4<=last;++p)if(!memcmp(p,"@spx",4)){stack=p;break;}
    if(stack) {const char *comma=memchr(space,',',(size_t)(last-space));if(!comma || strcmp(op,"mov"))return false;
      m.m[stack<comma?WDM_STACK_WRITE:WDM_STACK_READ]=1;}
    if(current<0)wd_add(prefix,&m);else wd_add(&metrics[current],&m);
  }
  return marker && (!original || entries==(unsigned)f->nodes[s->jump].jumpCount);
}

static bool
wide_domain_path_sum(wide_function *f,int target,const wide_domain_metric *metrics,wide_domain_metric *sum)
{
  int count=0;
  while(target>=0 && target<f->nn && count++<f->nn) {
    wide_node *n=&f->nodes[target];wd_add(sum,&metrics[target]);
    if(n->op==RETURN || n->op==ENDFUNCTION)return true;
    if(n->succ[0]<=target || n->succ[1]>=0 || n->jumpCount)return false;
    target=n->succ[0];
  }
  return false;
}

static bool
wide_domain_cost_paths(wide_function *old,wide_function *trial,wide_domain_shape *a,wide_domain_shape *b,
                       struct dbuf_s *oldCode,struct dbuf_s *newCode)
{
  wide_domain_metric *om=Safe_alloc(WN_MAX*sizeof *om),*nm=Safe_alloc(WN_MAX*sizeof *nm),op,np;
  bool valid=wide_domain_emitted_metrics(old,dbuf_get_buf(oldCode),dbuf_get_length(oldCode),a,true,om,&op) &&
    wide_domain_emitted_metrics(trial,dbuf_get_buf(newCode),dbuf_get_length(newCode),b,false,nm,&np);
  if(valid) {
    for(int i=0;i<a->compare;++i)wd_add(&op,&om[i]);
    /* 原范围检查整个按零计价形成严格下界，不把未走的默认边算作收益。
     * 新短派发所有比较/跳转都计入形成上界；保留尾部按实际路径逐项相加。 */
    wd_add(&op,&om[a->jump]);
    for(int i=0;i<=b->jump;++i)wd_add(&np,&nm[i]);
    for(unsigned k=0;k<a->count;++k) {
      wide_domain_metric x=op,y=np;
      bool known=wide_domain_path_sum(old,a->destinations[k],om,&x) &&
        wide_domain_path_sum(trial,b->destinations[k],nm,&y),better=known;
      for(unsigned m=0;m<WDM_COUNT;++m)if(y.m[m]>x.m[m])better=false;
      dbuf_printf(old->diagnostics,"; wide parameter-domain path value=%u known=%u accepted=%u old-lower-ins=%u new-upper-ins=%u old-lower-bytes=%u new-upper-bytes=%u\n",
        a->values[k],known,better,x.m[WDM_INS],y.m[WDM_INS],x.m[WDM_BYTES],y.m[WDM_BYTES]);
      if(!better)valid=false;
    }
  }
  Safe_free(om);Safe_free(nm);return valid;
}

static mcs251_wide_result
wide_choose_domain(wide_function *f,wide_selection_cost *cost)
{
  wide_domain_shape original,changed;
  if(!wide_domain_shape_check(f,&original))return wide_choose_allocation(f,cost);
  wide_function *trial=Safe_alloc(sizeof *trial);memcpy(trial,f,sizeof *trial);changed=original;
  if(!wide_domain_transform(trial,&changed)){Safe_free(trial);return wide_choose_allocation(f,cost);}
  struct dbuf_s oc,nc,od,nd;dbuf_init(&oc,1024);dbuf_init(&nc,1024);dbuf_init(&od,256);dbuf_init(&nd,256);
  struct dbuf_s *out=f->candidateOutput,*diag=f->diagnostics;
  f->candidateOutput=&oc;f->diagnostics=&od;trial->candidateOutput=&nc;trial->diagnostics=&nd;
  wide_selection_cost oldCost={0},newCost={0};
  mcs251_wide_result a=wide_choose_allocation(f,&oldCost),b=wide_choose_allocation(trial,&newCost);
  f->candidateOutput=out;f->diagnostics=diag;
  bool selected=a==MCS251_WIDE_ACCEPTED && b==MCS251_WIDE_ACCEPTED &&
    !oldCost.unknown && !newCost.unknown && newCost.bytes<oldCost.bytes &&
    newCost.heuristic<=oldCost.heuristic && !oldCost.frame && !newCost.frame &&
    !oldCost.spills && !newCost.spills && wide_domain_cost_paths(f,trial,&original,&changed,&oc,&nc);
  dbuf_printf(diag,"; wide parameter-domain choice function=%s ordinal=1 count=%u selected=%u old-bytes=%u new-bytes=%u old-unknown=%u new-unknown=%u old-frame=%d new-frame=%d old-spills=%u new-spills=%u values=",
    currFunc->name,original.count,selected,oldCost.bytes,newCost.bytes,oldCost.unknown,newCost.unknown,
    oldCost.frame,newCost.frame,oldCost.spills,newCost.spills);
  for(unsigned i=0;i<original.count;++i)dbuf_printf(diag,"%s%02x",i?",":"",original.values[i]);
  dbuf_append_str(diag,"\n");
  dbuf_append(diag,dbuf_get_buf(selected?&nd:&od),dbuf_get_length(selected?&nd:&od));
  if(a==MCS251_WIDE_ACCEPTED)dbuf_append(out,dbuf_get_buf(selected?&nc:&oc),dbuf_get_length(selected?&nc:&oc));
  if(cost)*cost=selected?newCost:oldCost;
  dbuf_destroy(&oc);dbuf_destroy(&nc);dbuf_destroy(&od);dbuf_destroy(&nd);Safe_free(trial);
  return b==MCS251_WIDE_ERROR?b:a;
}
