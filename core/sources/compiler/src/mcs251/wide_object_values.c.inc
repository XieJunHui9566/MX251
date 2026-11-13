/* SPDX-License-Identifier: GPL-2.0-or-later
 * Reuse ordinary object samples within one straight-line memory version.
 * Object identity distinguishes independent direct stores; an indirect store,
 * call, ordered access or CFG boundary ends all available memory facts.
 */
static unsigned
wide_object_values(wide_function *f)
{
  typedef struct {symbol *symbol;int object,offset,width;wide_operand value;} fact;
  fact facts[WN_MAX];int count=0,aliases[WV_MAX];unsigned changes=0;
  for(int v=0;v<f->nv;++v)aliases[v]=-1;
  for(int at=0;at<f->nn;++at) {
    wide_node *n=&f->nodes[at];
    bool boundary=wide_sink_block_barrier(n) || n->op==CALL || n->op==PCALL ||
      n->orderedReadCount || n->orderedAsmCount || n->memoryStateBarrier;
    if(boundary){count=0;for(int v=0;v<f->nv;++v)aliases[v]=-1;}
    /* A retained copy still defines the value for successors outside this
     * block. Inside the block, expose the same sampled operand to DIV/MOD. */
    wide_operand *operands[WA_MAX+7]={&n->left,&n->right,&n->addressIndex};
    unsigned operandsCount=3;
    for(unsigned k=0;k<n->stackArgs;++k)operands[operandsCount++]=&n->stackOperands[k];
    if(n->op==WM_BYTE_PACK)for(int k=0;k<n->width;++k)operands[operandsCount++]=&n->byteLanes[k];
    wide_bits use=n->use;
    for(unsigned k=0;k<operandsCount;++k) {
      wide_operand *o=operands[k];int v=o->value;
      if(v>=0 && wb_has(n->use,v) && aliases[v]>=0) {
        wide_bits one={{0}};wb_set(&one,v);use=wb_minus(use,one);
        o->value=aliases[v];wb_set(&use,o->value);
      }
    }
    n->use=use;
    for(int v=0;v<f->nv;++v)if(wb_has(n->def,v)) {
      aliases[v]=-1;
      for(int k=0;k<f->nv;++k)if(aliases[k]==v)aliases[k]=-1;
      for(int k=0;k<count;++k)if(facts[k].value.value==v)facts[k].symbol=NULL;
    }
    bool load=n->op==WM_SYMBOL_LOAD || n->op==WM_OBJECT_LOAD;
    bool store=n->op==WM_SYMBOL_STORE || n->op==WM_OBJECT_STORE;
    symbol *s=n->addressSymbol;
    bool direct=(load || store) && s && !n->byteAccess && !n->pointerAccess &&
      !n->addressOp && !isVolatile(s->type) && !isAtomic(s->type) &&
      !SPEC_ABSA(getSpec(s->type)) && SPEC_OCLS(s->etype)!=sfr;
    if(direct) {
      int found=-1;
      for(int k=0;k<count;++k)if(facts[k].symbol==s && facts[k].object==n->addressObject) {
        fact *p=&facts[k];
        if(store && p->offset<n->addressOffset+n->width && n->addressOffset<p->offset+p->width)p->symbol=NULL;
        else if(load && p->offset==n->addressOffset && p->width==n->width &&
                p->value.width==n->width && p->value.sign==n->sign &&
                p->value.floating==n->floating && !p->value.codePointer)found=k;
      }
      wide_operand value=store?n->right:(wide_operand){.value=n->result,.width=n->width,.sign=n->sign,.floating=n->floating};
      if(load && found>=0 && n->result>=0 && facts[found].value.value>=0 && facts[found].value.value!=n->result) {
        n->op='=';n->right=facts[found].value;n->left=(wide_operand){.value=-1};
        n->use=(wide_bits){{0}};wb_set(&n->use,n->right.value);
        n->addressSymbol=NULL;n->addressObject=-1;n->addressOffset=0;
        f->values[n->result].symbolSnapshot=false;aliases[n->result]=n->right.value;
        ++changes;
      } else if(value.value>=0 && value.width==n->width && count<WN_MAX)
        facts[count++]=(fact){s,n->addressObject,n->addressOffset,n->width,value};
    } else if(n->byteAccess || n->op==WM_STORE || n->op==WM_PARAM_STORE ||
              n->op==WM_BIT_STORE || n->op==WM_EDATA_STORE || store)count=0;
    if(n->succ[0]!=at+1 || n->succ[1]>=0 || n->jumpCount) {
      count=0;for(int v=0;v<f->nv;++v)aliases[v]=-1;
    }
  }
  if(changes) {
    wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide ordinary object values reused=%u\n",changes);
  }
  return changes;
}
