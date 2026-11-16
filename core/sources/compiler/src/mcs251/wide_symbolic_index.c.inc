/* SPDX-License-Identifier: GPL-2.0-or-later
 * Carry a constant object's symbol through an indexed memory transaction.
 * The base no longer needs a register or caller saves. The sampled index
 * and the load/store keep their original position and transaction width.
 */
static unsigned
wide_symbolic_index(wide_function *f,bool code)
{
  const char *enabled=getenv("MX251_SYMBOLIC_INDEX");
  if(enabled && !strcmp(enabled,"0"))return 0;
  bool found=false;
  for(int i=0;i<f->nn;++i)if((f->nodes[i].op==WM_LOAD || f->nodes[i].op==WM_STORE) &&
      !f->nodes[i].addressSymbol && f->nodes[i].codeAccess==code) {found=true;break;}
  if(!found)return 0;
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
         !(wide_fixed_address_value(&f->values[v]) ||
           (code && f->values[v].pointer && f->values[v].codePointer && f->values[v].width==4 &&
            !f->values[v].symbolSnapshot && !f->values[v].floating &&
            !isVolatile(f->values[v].sym->type) && !isAtomic(f->values[v].sym->type))))continue;
      wide_symbol_address a={0};int64_t offset=0;
      if(n->op==WM_ADDRESS && n->addressObject<0 && !n->addressOp) {
        a.symbol=n->addressSymbol;offset=n->addressOffset;
        if(!a.symbol || a.symbol->onStack || a.symbol->_isparm ||
           SPEC_SCLS(a.symbol->etype)==S_SFR || SPEC_SCLS(a.symbol->etype)==S_SBIT)continue;
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
      if(offset<0 || offset>getSize(a.symbol->type))continue;
      a.offset=(int)offset;address[v]=a;changed=true;
    }
  } while(changed);
  unsigned count=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int base=n->left.value,index=n->addressIndex.value;
    if((n->op!=WM_LOAD && n->op!=WM_STORE) || n->addressObject>=0 || n->addressSymbol ||
       n->codeAccess!=code || n->byteAccess || n->pointerAccess || n->orderedReadCount || n->nativeIndexed ||
       n->addressOp!='+' || base<0 || n->left.width!=4 || !address[base].symbol ||
       !wide_address_dominates(f,d,base,i) || index<0 ||
       n->addressIndex.sign || n->addressIndex.floating || n->addressIndex.codePointer ||
       f->values[index].pointer || (n->addressIndex.width!=1 && n->addressIndex.width!=2) ||
       (n->width!=1 && n->width!=2 && n->width!=4))continue;
    n->symbolicIndexed=true;n->addressSymbol=address[base].symbol;
    n->addressOffset=address[base].offset;n->left.value=-1;
    if(n->right.value!=base && index!=base) {
      wide_bits removed={{0}};wb_set(&removed,base);n->use=wb_minus(n->use,removed);
    }
    dbuf_printf(f->diagnostics,"; wide symbolic-index folded function=%s node=%d symbol=%s offset=%d width=%d index-width=%d\n",
      currFunc->name,i,n->addressSymbol->rname,n->addressOffset,n->width,n->addressIndex.width);
    ++count;
  }
  Safe_free(d);
  if(count) {wide_remove_dead_addresses(f);wide_reset_allocation(f);}
  return count;
}

static unsigned wide_symbolic_data_index(wide_function *f)
{return wide_symbolic_index(f,false);}

static unsigned wide_symbolic_code_index(wide_function *f)
{return wide_symbolic_index(f,true);}
