/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keep a local object's address as an object/offset call operand. ABI setup
 * forms it from the current SPX, after caller saves and earlier arguments.
 */
static unsigned
wide_stack_call_addresses(wide_function *f)
{
  if(!f->no)return 0;
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  wide_symbol_address address[WV_MAX]={{0}};
  for(int v=0;v<f->nv;++v)d->definition[v]=-1;
  for(int i=0;i<f->nn;++i) {
    if(f->nodes[i].op==FUNCTION)d->entry=i;
    for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].def,v)) {
      ++d->definitions[v];d->definition[v]=i;
    }
  }
  if(d->entry<0) {Safe_free(d);return 0;}
  wide_address_reachable(f,d->entry,-1,d->reachable);bool changed;
  do {
    changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];int v=n->result,source=-1;
      if(v<0 || address[v].symbol || !d->reachable[i] || d->definitions[v]!=1 ||
         !wide_fixed_address_value(&f->values[v]))continue;
      wide_symbol_address a={0};int64_t offset=0;
      if(n->op==WM_ADDRESS && n->addressObject>=0 && !n->addressOp) {
        a=(wide_symbol_address){.symbol=f->objects[n->addressObject].sym,
          .object=n->addressObject,.stack=true};offset=n->addressOffset;
      } else {
        int64_t delta=0;
        if((n->op=='=' || n->op==CAST) && n->width==4 && n->right.width==4)source=n->right.value;
        else if((n->op=='+' || n->op=='-') && n->width==4 && n->left.width==4 &&
                wide_fixed_address_delta(n->right,&delta)) {
          source=n->left.value;if(n->op=='-')delta=-delta;
        }
        if(source<0 || !address[source].symbol || !wide_address_dominates(f,d,source,i))continue;
        a=address[source];offset=a.offset+delta;
      }
      if(offset<0 || offset>f->objects[a.object].size)continue;
      a.offset=(int)offset;address[v]=a;changed=true;
    }
  } while(changed);
  unsigned count=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];if(n->op!=CALL || !n->ordinaryCall || n->indirectCall)continue;
    bool replaced=false;
    for(unsigned k=0;k<=n->stackArgs;++k) {
      if(!k && (!n->args || n->variadicCall))continue;
      wide_operand *o=k?&n->stackOperands[k-1]:&n->left;
      unsigned physical=k?n->stackWidths[k-1]:getSize(FUNC_ARGS(wide_call_type(n))->type);
      if(physical!=3 || o->value<0 || !address[o->value].symbol ||
         !wide_address_dominates(f,d,o->value,i))continue;
      n->callAddresses[k]=address[o->value];o->value=-1;o->literal=0;
      replaced=true;++count;
    }
    if(replaced) {
      n->use=(wide_bits){{0}};wb_set(&n->use,n->left.value);wb_set(&n->use,n->right.value);
      for(unsigned k=0;k<n->stackArgs;++k)wb_set(&n->use,n->stackOperands[k].value);
    }
  }
  Safe_free(d);
  if(count) {
    wide_remove_dead_addresses(f);wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide stack call addresses=%u\n",count);
  }
  return count;
}
