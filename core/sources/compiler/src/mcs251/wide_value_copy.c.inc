/* SPDX-License-Identifier: GPL-2.0-or-later
 * Available value copies on the complete MIR CFG. Facts describe sampled
 * virtual values, never the contents of the memory from which they came.
 * Merge overlapping copies: a source that dies at its copy is already a
 * register-coalescing opportunity, not duplicated live storage.
 */
static bool
wide_copy_same_representation(wide_value *a,wide_value *b)
{
  if(a->pointer!=b->pointer || a->codePointer!=b->codePointer ||
     a->floating!=b->floating || a->nearData!=b->nearData)return false;
  if(a->sign!=b->sign) {
    const char *enabled=getenv("MX251_INTEGER_VIEW_COPIES");
    if((enabled && !strcmp(enabled,"0")) || a->pointer || a->codePointer ||
       a->floating || a->nearData)return false;
    /* Equal-width integer conversions preserve the stored bits. Each use
     * retains its own sign/width, which selects extension, comparison and
     * shift semantics independently of the allocation identity. Boolean
     * normalization has its own WM_BIT_CAST node and is not a copy. */
  }
  if(a->pointer && (!IS_PTR(a->sym->type) || !IS_PTR(b->sym->type) ||
     DCL_TYPE(a->sym->type)!=DCL_TYPE(b->sym->type)))return false;
  return true;
}

static int
wide_copy_source(wide_function *f,wide_node *n)
{
  if((n->op!='=' && n->op!=CAST) || n->result<0 || n->right.value<0 ||
     n->result==n->right.value || !wb_has(n->out,n->right.value) ||
     n->orderedReadCount || n->orderedAsmCount ||
     n->byteAccess || n->addressOp || n->addressSymbol || n->addressObject>=0)return -1;
  wide_value *a=&f->values[n->right.value],*b=&f->values[n->result];
  if(a->width!=b->width || n->width!=b->width || n->right.width!=a->width ||
     !wide_copy_same_representation(a,b) || b->symbolSnapshot ||
     b->pin>=0)return -1;
  return n->right.value;
}

static int
wide_copy_root(int value,const int *source,wide_bits available)
{
  for(int hops=0;value>=0 && source[value]>=0 && wb_has(available,value);++hops) {
    wassert(hops<WV_MAX);
    if(hops>=WV_MAX)break;
    value=source[value];
  }
  return value;
}

static unsigned
wide_value_copies(wide_function *f)
{
  int nv=f->nv;
  wassert(nv>=0 && nv<=WV_MAX);
  if(nv<0 || nv>WV_MAX)return 0;
  int source[WV_MAX],definition[WV_MAX],definitions[WV_MAX]={0},entry=-1;
  wide_bits universe={{0}};unsigned candidates=0;
  for(int v=0;v<nv;++v)source[v]=definition[v]=-1;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==FUNCTION)entry=i;
    for(int v=0;v<nv;++v)if(wb_has(n->def,v)) {
      ++definitions[v];definition[v]=i;
    }
  }
  if(entry<0)return 0;
  for(int v=0;v<nv;++v)if(definitions[v]==1) {
    source[v]=wide_copy_source(f,&f->nodes[definition[v]]);
    if(source[v]>=0) {wb_set(&universe,v);++candidates;}
  }
  if(!candidates)return 0;
  unsigned char reachable[WN_MAX];wide_address_reachable(f,entry,-1,reachable);
  wide_bits *in=Safe_alloc(f->nn*sizeof *in),*next=Safe_alloc(f->nn*sizeof *next);
  wide_bits *kill=Safe_alloc(f->nn*sizeof *kill),*gen=Safe_alloc(f->nn*sizeof *gen);
  for(int i=0;i<f->nn;++i) {
    in[i]=universe;kill[i]=gen[i]=(wide_bits){{0}};
    for(int v=0;v<nv;++v)if(source[v]>=0) {
      if(wb_has(f->nodes[i].def,v) || wb_has(f->nodes[i].def,source[v]))wb_set(&kill[i],v);
      if(definition[v]==i)wb_set(&gen[i],v);
    }
  }
  in[entry]=(wide_bits){{0}};
  bool changed;
  do {
    for(int i=0;i<f->nn;++i)next[i]=universe;
    next[entry]=(wide_bits){{0}};
    for(int i=0;i<f->nn;++i)if(reachable[i]) {
      wide_node *n=&f->nodes[i];wide_bits out=wb_or(wb_minus(in[i],kill[i]),gen[i]);
      for(int k=0;k<2+n->jumpCount;++k) {
        int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
        if(to<0)continue;
        for(int w=0;w<WB_WORDS;++w)next[to].w[w]&=out.w[w];
      }
    }
    changed=false;
    for(int i=0;i<f->nn;++i)if(reachable[i] && !wb_equal(in[i],next[i])) {
      in[i]=next[i];changed=true;
    }
  } while(changed);
  unsigned replacements=0,removed=0;
  for(int i=0;i<f->nn;++i)if(reachable[i]) {
    wide_node *n=&f->nodes[i];wide_bits uses={{0}};
    for(int v=0;v<nv;++v)if(wb_has(n->use,v))wb_set(&uses,wide_copy_root(v,source,in[i]));
    wide_operand *operands[WA_MAX+3]={&n->left,&n->right,&n->addressIndex};
    for(unsigned k=0;k<n->stackArgs;++k)operands[k+3]=&n->stackOperands[k];
    for(unsigned k=0;k<3+n->stackArgs;++k) {
      wide_operand *o=operands[k];
      if(!wb_has(n->use,o->value))continue;
      int root=wide_copy_root(o->value,source,in[i]);
      if(root!=o->value) {o->value=root;++replacements;}
    }
    n->use=uses;
  }
  wide_bits used={{0}};
  for(int i=0;i<f->nn;++i)used=wb_or(used,f->nodes[i].use);
  for(int v=0;v<nv;++v)if(source[v]>=0 && !wb_has(used,v)) {
    wide_node *n=&f->nodes[definition[v]];
    n->op=WM_NOP;n->result=n->left.value=n->right.value=n->addressIndex.value=-1;
    n->use=n->def=(wide_bits){{0}};++removed;
  }
  Safe_free(gen);Safe_free(kill);Safe_free(next);Safe_free(in);
  if(replacements || removed) {
    wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide value-copy propagated candidates=%u operands=%u removed=%u\n",candidates,replacements,removed);
  }
  return replacements+removed;
}
