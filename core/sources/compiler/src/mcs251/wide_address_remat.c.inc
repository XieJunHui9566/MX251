/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keep a derived address as base/index operands of its memory transactions.
 * A recipe can cross calls and CFG edges while its sampled operands retain
 * their values. Loads/stores stay at their original nodes and retain widths.
 */
static void
wide_address_recipe_stale(wide_function *f,int definition,wide_bits inputs,
  unsigned char stale[WN_MAX])
{
  unsigned char seen[WN_MAX][2]={{0}};
  int pending[WN_MAX*2],head=0,tail=0;
  /* The defining node refreshes the recipe on every iteration. Track both
   * clean and overwritten paths until that definition executes again. */
  pending[tail++]=2*definition;seen[definition][0]=1;
  while(head<tail) {
    int state=pending[head++],at=state/2,dirty=state&1;
    wide_node *n=&f->nodes[at];
    if(dirty)stale[at]=1;
    if(at!=definition)for(int v=0;v<f->nv;++v)
      if(wb_has(inputs,v) && wb_has(n->def,v)) {dirty=1;break;}
    for(int edge=0;edge<2+n->jumpCount;++edge) {
      int next=edge<2?n->succ[edge]:f->jumpSuccessors[n->jumpStart+edge-2];
      if(next<0 || next==definition || seen[next][dirty])continue;
      seen[next][dirty]=1;pending[tail++]=2*next+dirty;
    }
  }
}

static bool
wide_address_recipe_memory(wide_node *n,int address)
{
  return (n->op==WM_LOAD || n->op==WM_STORE) && n->left.value==address &&
    n->right.value!=address && n->addressIndex.value!=address &&
    n->left.width==4 && n->addressObject<0 && !n->addressSymbol &&
    !n->byteAccess && !n->pointerAccess && !n->orderedReadCount &&
    (!n->codeAccess || mcs251FlatCodeReadsEnabled) && !n->nativeIndexed;
}

static unsigned
wide_rematerialize_addresses(wide_function *f)
{
  const char *enabled=getenv("MX251_ADDRESS_REMATERIALIZATION");
  if(enabled && !strcmp(enabled,"0"))return 0;
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  for(int v=0;v<f->nv;++v)d->definition[v]=-1;
  for(int i=0;i<f->nn;++i) {
    if(f->nodes[i].op==FUNCTION)d->entry=i;
    for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].def,v)) {
      ++d->definitions[v];d->definition[v]=i;
    }
  }
  if(d->entry<0) {Safe_free(d);return 0;}
  wide_address_reachable(f,d->entry,-1,d->reachable);
  unsigned recipes=0,accesses=0;
  for(int i=f->nn-1;i>=0;--i) {
    wide_node *a=&f->nodes[i];int value=a->result;
    if((a->op!='+' && a->op!='-') || value<0 || a->width!=4 ||
       !f->values[value].pointer || f->values[value].symbolSnapshot ||
       f->values[value].pin>=0 || !f->values[value].sym ||
       f->values[value].sym->addrtaken || d->definitions[value]!=1 ||
       !d->reachable[i] || a->left.width!=4 || a->left.value==value ||
       a->right.value==value || a->right.floating ||
       (a->left.codePointer && !mcs251FlatCodeReadsEnabled) ||
       (a->right.value>=0 && f->values[a->right.value].pointer))continue;
    bool stable=true;
    for(int v=0;v<f->nv;++v)if(wb_has(a->use,v)) {
      wide_value *input=&f->values[v];
      if(input->symbolSnapshot || !input->sym || input->sym->addrtaken ||
         isVolatile(input->sym->type) || isAtomic(input->sym->type))stable=false;
    }
    if(!stable)continue;
    unsigned char stale[WN_MAX]={0};
    wide_address_recipe_stale(f,i,a->use,stale);
    unsigned uses=0;
    for(int j=0;j<f->nn && stable;++j)if(wb_has(f->nodes[j].use,value)) {
      wide_node *n=&f->nodes[j];int64_t x,y;
      if(!wide_address_recipe_memory(n,value) || stale[j] ||
         !wide_address_dominates(f,d,value,j)) {stable=false;break;}
      if(n->addressOp && ((n->addressOp!='+' && n->addressOp!='-') ||
         !wide_fixed_address_delta(a->right,&x) ||
         !wide_fixed_address_delta(n->addressIndex,&y))) {stable=false;break;}
      ++uses;
    }
    if(!stable || !uses)continue;
    for(int j=0;j<f->nn;++j)if(wb_has(f->nodes[j].use,value)) {
      wide_node *n=&f->nodes[j];
      if(n->addressOp) {
        int64_t x=0,y=0;wide_fixed_address_delta(a->right,&x);
        wide_fixed_address_delta(n->addressIndex,&y);
        x=(a->op=='-'?-x:x)+(n->addressOp=='-'?-y:y);
        n->addressIndex=(wide_operand){.value=-1,.width=4,.literal=(uint32_t)x&0xffffff};
        n->addressOp='+';
      } else {n->addressIndex=a->right;n->addressOp=a->op;}
      n->left=a->left;
      n->use=wb_or(wb_minus(n->use,a->def),a->use);
    }
    dbuf_printf(f->diagnostics,"; wide address-remat recipe function=%s value=%d node=%d accesses=%u\n",
      currFunc->name,value,i,uses);
    a->op=WM_NOP;a->result=a->left.value=a->right.value=a->addressIndex.value=-1;
    a->use=a->def=(wide_bits){{0}};
    ++recipes;accesses+=uses;
  }
  Safe_free(d);
  if(recipes) {
    wide_remove_dead_addresses(f);wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide address-remat summary recipes=%u accesses=%u\n",recipes,accesses);
  }
  return recipes;
}
