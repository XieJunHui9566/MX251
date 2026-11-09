/* SPDX-License-Identifier: GPL-2.0-or-later
 * Recover physical parameter slots after inlining address-based helpers.
 * A remaining address use keeps the slot observable; complete accesses can
 * nevertheless use SPX directly, in their original execution order.
 */
static unsigned
wide_parameter_addresses(wide_function *f)
{
  bool addressed=false;
  for(int i=0;i<f->nn;++i)addressed|=f->nodes[i].op==WM_PARAM_ADDRESS;
  if(!addressed)return 0;
  symbol *symbols[WA_MAX]={0};int slots[WA_MAX]={0},sizes[WA_MAX]={0};
  unsigned count=0,index=0;int at=-2;
  for(value *arg=FUNC_ARGS(currFunc->type);arg;arg=arg->next,++index) {
    if(index<wide_register_count(currFunc->type))continue;
    if(count==WA_MAX)return 0;
    at-=getSize(arg->type);symbols[count]=arg->sym;
    slots[count]=at;sizes[count++]=getSize(arg->type);
  }
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  int parameter[WV_MAX],offset[WV_MAX]={0};unsigned folded=0;
  for(int v=0;v<f->nv;++v)parameter[v]=d->definition[v]=-1;
  for(int i=0;i<f->nn;++i) {
    if(f->nodes[i].op==FUNCTION)d->entry=i;
    for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].def,v)) {
      ++d->definitions[v];d->definition[v]=i;
    }
  }
  if(d->entry<0) {Safe_free(d);return 0;}
  wide_address_reachable(f,d->entry,-1,d->reachable);
  bool changed;
  do {
    changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];int v=n->result,p=-1;int64_t position=0;
      if(v<0 || !d->reachable[i] || d->definitions[v]!=1 || d->definition[v]!=i ||
         parameter[v]>=0 || !wide_fixed_address_value(&f->values[v]))continue;
      if(n->op==WM_PARAM_ADDRESS) {
        for(unsigned k=0;k<count;++k)if(n->addressSymbol==symbols[k]) {
          p=(int)k;position=n->addressOffset-slots[k];break;
        }
      } else {
        int source=-1;int64_t delta=0;
        if((n->op=='=' || n->op==CAST) && n->width==4 && n->right.width==4)
          source=n->right.value;
        else if((n->op=='+' || n->op=='-') && n->width==4 && n->left.width==4 &&
                wide_fixed_address_delta(n->right,&delta)) {
          source=n->left.value;if(n->op=='-')delta=-delta;
        } else continue;
        /* FAR and generic RAM pointers share the flat 24-bit representation.
         * Both endpoints passed wide_fixed_address_value, which excludes
         * CODE, near/paged pointers and integer casts. */
        if(source<0 || parameter[source]<0 || !wide_address_dominates(f,d,source,i))continue;
        p=parameter[source];position=offset[source]+delta;
      }
      if(p>=0 && position>=0 && position<=sizes[p]) {
        parameter[v]=p;offset[v]=(int)position;changed=true;
      }
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int source=n->left.value;
    if((n->op!=WM_LOAD && n->op!=WM_STORE) || n->addressObject>=0 || n->codeAccess ||
       n->byteAccess || n->orderedReadCount || source<0 || n->left.width!=4 ||
       parameter[source]<0 || !wide_address_dominates(f,d,source,i))continue;
    int p=parameter[source],physical=n->pointerAccess?3:n->width;
    int64_t position=offset[source],delta=0;
    if(n->addressOp) {
      if((n->addressOp!='+' && n->addressOp!='-') ||
         !wide_fixed_address_delta(n->addressIndex,&delta))continue;
      position+=n->addressOp=='+'?delta:-delta;
    }
    /* Whole-slot transactions match the existing parameter emitter. Partial
     * accesses keep their address, which also prevents value promotion. */
    if(position || physical!=sizes[p])continue;
    n->op=n->op==WM_LOAD?WM_PARAM_LOAD:WM_PARAM_STORE;
    n->addressSymbol=symbols[p];n->addressOffset=slots[p];
    n->left=(wide_operand){.value=-1,.width=physical,.sign=n->sign};
    n->addressOp=0;n->addressIndex=(wide_operand){.value=-1};
    n->use=(wide_bits){{0}};wb_set(&n->use,n->right.value);
    dbuf_printf(f->diagnostics,"; wide parameter-address folded slot=%d physical=%d node=%d source=%d\n",
      slots[p],physical,i,source);
    ++folded;
  }
  Safe_free(d);
  if(folded) {wide_remove_dead_addresses(f);wide_reset_allocation(f);}
  return folded;
}
