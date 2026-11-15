/* SPDX-License-Identifier: GPL-2.0-or-later
 * Available copies belong to definitions, not to mutable value names. A
 * redefinition of either endpoint kills the fact; joins intersect facts.
 * This permits copies into promoted locals and copies whose source dies.
 */
static int
wide_definition_copy(wide_function *f,wide_node *n)
{
  if((n->op!='=' && n->op!=CAST && n->op!=WM_RAW32_COPY) || n->result<0 || n->right.value<0 ||
     n->result==n->right.value || n->orderedReadCount || n->orderedAsmCount ||
     n->byteAccess || n->addressOp || n->addressSymbol || n->addressObject>=0)return -1;
  wide_value *a=&f->values[n->right.value],*b=&f->values[n->result];
  if(a->width!=b->width || n->width!=b->width || n->right.width!=a->width || b->pin>=0)return -1;
  if(n->op==WM_RAW32_COPY) {
    /* Operand types remain on each use. This identity concerns the 32 bits,
     * not a conversion between the source and destination C types. */
    if(a->width!=4 || a->pointer || b->pointer)return -1;
  } else if(!wide_copy_same_representation(a,b))return -1;
  return n->right.value;
}

static int
wide_definition_root(int value,const int *source)
{
  for(int hops=0;value>=0 && source[value]>=0;++hops) {
    wassert(hops<WV_MAX);if(hops>=WV_MAX)break;
    value=source[value];
  }
  return value;
}

static unsigned
wide_definition_copies(wide_function *f)
{
  int source[WV_MAX],destination[WV_MAX],definition[WV_MAX],entry=-1,count=0;
  wide_bits universe={{0}};
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==FUNCTION)entry=i;
    int from=wide_definition_copy(f,n);
    if(from>=0 && count<WV_MAX) {
      source[count]=from;destination[count]=n->result;definition[count]=i;
      wb_set(&universe,count++);
    }
  }
  if(entry<0 || !count)return 0;
  unsigned char reachable[WN_MAX];wide_address_reachable(f,entry,-1,reachable);
  wide_bits *in=Safe_alloc(f->nn*sizeof *in),*next=Safe_alloc(f->nn*sizeof *next);
  wide_bits *kill=Safe_alloc(f->nn*sizeof *kill),*gen=Safe_alloc(f->nn*sizeof *gen);
  for(int i=0;i<f->nn;++i) {
    in[i]=universe;kill[i]=gen[i]=(wide_bits){{0}};
    for(int c=0;c<count && c<WV_MAX;++c) {
      if(wb_has(f->nodes[i].def,source[c]) || wb_has(f->nodes[i].def,destination[c]))wb_set(&kill[i],c);
      if(definition[c]==i)wb_set(&gen[i],c);
    }
  }
  in[entry]=(wide_bits){{0}};bool changed;
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
    wide_node *n=&f->nodes[i];int roots[WV_MAX];
    for(int v=0;v<f->nv;++v)roots[v]=-1;
    for(int c=0;c<count && c<WV_MAX;++c)if(wb_has(in[i],c))roots[destination[c]]=source[c];
    wide_bits uses={{0}};
    for(int v=0;v<f->nv;++v)if(wb_has(n->use,v))wb_set(&uses,wide_definition_root(v,roots));
    wide_operand *operands[WA_MAX+3]={&n->left,&n->right,&n->addressIndex};
    for(unsigned k=0;k<n->stackArgs;++k)operands[k+3]=&n->stackOperands[k];
    for(unsigned k=0;k<3+n->stackArgs;++k) {
      wide_operand *o=operands[k];if(!wb_has(n->use,o->value))continue;
      int root=wide_definition_root(o->value,roots);
      if(root!=o->value) {o->value=root;++replacements;}
    }
    n->use=uses;
  }
  Safe_free(gen);Safe_free(kill);Safe_free(next);Safe_free(in);
  if(!replacements)return 0;
  /* Dead stores to a virtual home are separate from real memory stores.
   * Recompute liveness after rewrites, including loops, before removing them. */
  do {
    wide_reset_allocation(f);wide_liveness(f);changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(wide_definition_copy(f,n)<0 || wb_has(n->out,n->result))continue;
      n->op=WM_NOP;n->result=n->left.value=n->right.value=n->addressIndex.value=-1;
      n->use=n->def=(wide_bits){{0}};changed=true;++removed;
    }
  } while(changed);
  wide_reset_allocation(f);
  dbuf_printf(f->diagnostics,"; wide definition-copy facts=%d operands=%u removed=%u\n",count,replacements,removed);
  return replacements+removed;
}
