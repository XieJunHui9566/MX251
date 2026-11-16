/* Symbol addresses are linker constants, not memory samples. Keep them in
 * call operands until ABI emission instead of allocating a four-byte copy.
 * Only single-definition address/copy chains participate. Mutable pointer
 * objects, local stack addresses and indirect loads remain ordinary values. */
static void
wide_constant_call_addresses(wide_function *f)
{
  int definitions[WV_MAX];wide_symbol_address address[WV_MAX]={{0}};
  for(int v=0;v<f->nv;++v)definitions[v]=-1;
  for(int i=0;i<f->nn;++i)for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].def,v))
    definitions[v]=definitions[v]==-1?i:-2;
  bool changed;
  do {
    changed=false;
    for(int v=0;v<f->nv;++v) {
      if(address[v].symbol || definitions[v]<0 || !f->values[v].pointer)continue;
      wide_node *n=&f->nodes[definitions[v]];
      if(n->op==WM_ADDRESS && n->addressObject<0 && n->addressSymbol &&
         !n->addressSymbol->onStack && !n->addressOp) {
        address[v]=(wide_symbol_address){n->addressSymbol,n->addressOffset};changed=true;
      } else if((n->op=='=' || n->op==CAST) && n->right.value>=0 &&
                n->right.width==4 && address[n->right.value].symbol) {
        address[v]=address[n->right.value];changed=true;
      }
    }
  } while(changed);
  unsigned count=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if((n->op==WM_EDATA_STORE || n->op==WM_STORE) && n->pointerAccess && n->width==4 &&
       n->addressObject<0 && n->right.value>=0 && address[n->right.value].symbol) {
      int value=n->right.value;
      n->storedAddress=address[value];n->right.value=-1;n->right.literal=0;
      if(n->left.value!=value && n->addressIndex.value!=value) {
        wide_bits removed={{0}};wb_set(&removed,value);n->use=wb_minus(n->use,removed);
      }
      ++count;continue;
    }
    if(n->op!=CALL || !n->ordinaryCall || n->indirectCall)continue;
    bool replaced=false;
    for(unsigned k=0;k<=n->stackArgs;++k) {
      if(!k && (!n->args || n->variadicCall))continue;
      wide_operand *o=k?&n->stackOperands[k-1]:&n->left;
      unsigned physical=k?n->stackWidths[k-1]:getSize(FUNC_ARGS(wide_call_type(n))->type);
      if(physical!=3 || o->value<0 || !address[o->value].symbol)continue;
      n->callAddresses[k]=address[o->value];
      o->value=-1;o->literal=0;replaced=true;++count;
    }
    if(replaced) {
      n->use=(wide_bits){{0}};wb_set(&n->use,n->left.value);wb_set(&n->use,n->right.value);
      for(unsigned k=0;k<n->stackArgs;++k)wb_set(&n->use,n->stackOperands[k].value);
    }
  }
  if(!count)return;
  /* Drop only the now-unused pure address definitions, including copies.
   * A surviving dereference, return or comparison keeps its original chain. */
  do {
    wide_bits used={{0}};changed=false;
    for(int i=0;i<f->nn;++i)used=wb_or(used,f->nodes[i].use);
    for(int v=0;v<f->nv;++v)if(address[v].symbol && !wb_has(used,v)) {
      wide_node *n=&f->nodes[definitions[v]];
      if(n->op==WM_NOP)continue;
      n->op=WM_NOP;n->use=n->def=(wide_bits){{0}};changed=true;
    }
  } while(changed);
  wide_reset_allocation(f);
  dbuf_printf(f->diagnostics,"; wide symbolic call addresses=%u\n",count);
}
