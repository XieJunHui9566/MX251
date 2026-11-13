/* SPDX-License-Identifier: GPL-2.0-or-later
 * Reuse loop-private object bases as byte-stride cursors. The original
 * counter, memory transactions and exit remain in place; the enclosing
 * selector prices initialization, allocation and every emitted loop. */
typedef struct { int base, definition; unsigned width; } wide_cursor_stream;

static unsigned
wide_loop_cursors(wide_function *f)
{
  const char *option=getenv("MX251_LOOP_CURSORS");
  if(option && !strcmp(option,"0"))return 0;
  unsigned changed=0;
  /* Work backwards so insertion does not move unexamined loop latches. */
  for(int latch=f->nn-2;latch>=4;--latch) {
    wide_node *branch=&f->nodes[latch],*cmp=&f->nodes[latch-1],*step=&f->nodes[latch-2];
    if(branch->op!=IFX || !branch->trueBranch || branch->succ[0]!=latch+1 ||
       cmp->op!='<' || cmp->result<0 || branch->left.value!=cmp->result ||
       cmp->left.value<0 || cmp->left.sign || cmp->left.floating ||
       cmp->right.value>=0 || cmp->right.sign || cmp->right.floating ||
       cmp->left.width!=cmp->right.width || step->op!='+' ||
       step->result!=cmp->left.value || step->left.value!=step->result ||
       (step->width!=1 && step->width!=2) || cmp->left.width!=step->width ||
       step->left.width!=step->width || !wide_affine_literal(step->right,step->width,1))continue;
    int index=step->result,first=branch->succ[1];
    uint32_t bound=cmp->right.literal;
    if(first<=0 || first>=latch-2 || f->nodes[first].op!=LABEL ||
       !bound || bound>(step->width==1?255u:65535u) ||
       !wide_affine_value(f,index,step->width,false))continue;
    while(first>0 && f->nodes[first-1].op==LABEL)--first;
    int init=first-1;
    if(init<0 || f->nodes[init].op!='=' || f->nodes[init].result!=index ||
       !wide_affine_literal(f->nodes[init].right,step->width,0))continue;
    bool valid=true;
    for(int i=init;i<latch;++i) {
      wide_node *n=&f->nodes[i];
      if(n->succ[0]!=i+1 || n->succ[1]>=0 || n->jumpCount ||
         n->op==CALL || n->op==PCALL || n->op==WM_ORDERED_ASM ||
         n->memoryStateBarrier || (i!=init && i!=latch-2 && wb_has(n->def,index)))valid=false;
    }
    if(!valid)continue;
    wide_cursor_stream streams[8];int count=0,earliest=init;
    int streamAt[WN_MAX],addressAt[WN_MAX];
    for(int i=0;i<f->nn;++i)streamAt[i]=addressAt[i]=-1;
    for(int i=first;i<latch-2 && valid;++i) {
      wide_node *n=&f->nodes[i];
      if((n->op!=WM_LOAD && n->op!=WM_STORE) || n->byteAccess || n->pointerAccess ||
         n->addressObject>=0 || n->left.value<0 || n->left.width!=4 ||
         (n->codeAccess && !mcs251FlatCodeReadsEnabled))continue;
      int base=-1,addition=-1;
      if(n->addressOp=='+' && n->addressIndex.value==index &&
         n->addressIndex.width==step->width && !n->addressIndex.sign)base=n->left.value;
      else if(!n->addressOp) {
        for(int a=first;a<i;++a) {
          wide_node *p=&f->nodes[a];
          if(p->op!='+' || p->result!=n->left.value || p->width!=4 ||
             p->left.width!=4 || p->right.value!=index ||
             p->right.width!=step->width || p->right.sign)continue;
          if(wide_affine_only_use_def(f,p->result,a,-1,i,-1,-1,false)) {
            base=p->left.value;addition=a;
          }
          break;
        }
      }
      if(base<0)continue;
      int s=0;while(s<count && streams[s].base!=base)++s;
      if(s==count) {
        if(count==8){valid=false;break;}
        int def=-1;
        for(int k=0;k<f->nn;++k)if(wb_has(f->nodes[k].def,base)) {
          if(def>=0){def=-2;break;}def=k;
        }
        if(def<0 || def>=init)continue;
        wide_node *root=&f->nodes[def];wide_value *v=&f->values[base];
        symbol *object=root->addressSymbol;
        if(root->op!=WM_ADDRESS || root->addressObject>=0 || root->addressOp ||
           root->addressOffset<0 || !object || !IS_ARRAY(object->type) || object->onStack ||
           object->_isparm || IS_EXTERN(object->etype) || SPEC_ABSA(object->etype) ||
           !v->pointer || v->width!=4 || v->symbolSnapshot || !v->sym ||
           v->sym->addrtaken || isVolatile(v->sym->type) || isAtomic(v->sym->type))continue;
        memmap *space=SPEC_OCLS(object->etype);
        if((!IN_FARSPACE(space) && !IN_CODESPACE(space)) || space->paged ||
           getSize(object->type)<=0)continue;
        streams[count++]=(wide_cursor_stream){base,def,0};
        if(def<earliest)earliest=def;
      }
      if(n->width>streams[s].width)streams[s].width=n->width;
      streamAt[i]=s;addressAt[i]=addition;
    }
    if(!valid || !count || f->nn+count>f->nodeLimit || f->cursorCount==32)continue;
    /* Bases are recreated on every entry, used only by these address streams,
     * and never observed at their final one-past value. */
    for(int s=0;s<count && valid;++s) {
      wide_node *root=&f->nodes[streams[s].definition];
      if((uint64_t)root->addressOffset+bound-1+streams[s].width>
         (unsigned)getSize(root->addressSymbol->type)){valid=false;break;}
      for(int i=0;i<f->nn && valid;++i)if(wb_has(f->nodes[i].use,streams[s].base)) {
        bool matched=streamAt[i]==s && addressAt[i]<0 && f->nodes[i].right.value!=streams[s].base;
        for(int m=first;m<latch-2 && !matched;++m)
          if(streamAt[m]==s && addressAt[m]==i)matched=true;
        if(!matched)valid=false;
      }
    }
    for(int i=earliest;i<first && valid;++i) {
      wide_node *n=&f->nodes[i];
      if(n->succ[0]!=i+1 || n->succ[1]>=0 || n->jumpCount)valid=false;
    }
    for(int i=0;i<f->nn && valid;++i) {
      wide_node *n=&f->nodes[i];
      for(int k=0;k<2+n->jumpCount;++k) {
        int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
        if(to>earliest && to<=latch &&
           !((k==0 && i>=earliest && i<latch && to==i+1) ||
             (i==latch && k==1 && to==branch->succ[1])))valid=false;
      }
    }
    if(!valid)continue;
    for(int i=first;i<latch-2;++i)if(streamAt[i]>=0) {
      wide_node *n=&f->nodes[i];int base=streams[streamAt[i]].base,a=addressAt[i];
      if(a>=0) {
        wide_node *p=&f->nodes[a];
        n->use=wb_minus(n->use,p->def);
        p->op=WM_NOP;p->result=p->left.value=p->right.value=-1;
        p->use=p->def=(wide_bits){{0}};
      } else {
        wide_bits bit={{0}};wb_set(&bit,index);n->use=wb_minus(n->use,bit);
        /* A stored value may itself be the counter. */
        if(n->right.value==index)wb_set(&n->use,index);
      }
      n->left=(wide_operand){.value=base,.width=4,.codePointer=f->values[base].codePointer};
      wb_set(&n->use,base);n->addressOp=0;n->addressIndex=(wide_operand){.value=-1};
      n->nativeCodeByte=n->codeAccess && n->width==1;
    }
    f->cursorLabels[f->cursorCount++]=branch->target;
    int at=latch-2,oldCount=f->nn;wide_node increment=*step;
    for(int i=0;i<oldCount;++i)for(int k=0;k<2;++k)
      if(f->nodes[i].succ[k]>=at)f->nodes[i].succ[k]+=count;
    for(int i=0;i<f->nj;++i)if(f->jumpSuccessors[i]>=at)f->jumpSuccessors[i]+=count;
    memmove(&f->nodes[at+count],&f->nodes[at],(oldCount-at)*sizeof f->nodes[0]);
    f->nn+=count;f->nodes[at-1].succ[0]=at;
    for(int s=0;s<count;++s) {
      wide_node *n=&f->nodes[at+s];*n=increment;
      int base=streams[s].base;
      n->width=4;n->result=base;
      n->left=(wide_operand){.value=base,.width=4,.codePointer=f->values[base].codePointer};
      n->right=(wide_operand){.value=-1,.width=4,.literal=1};
      /* Every observed cursor is inside this allocated 24-bit object. The
       * final increment may reach one-past, but has no consumer after exit.
       * Keep the 32-bit increment and carry this proof to its emission. */
      n->boundedCursorStep=true;
      n->use=n->def=(wide_bits){{0}};wb_set(&n->use,base);wb_set(&n->def,base);
      n->in=n->out=(wide_bits){{0}};n->succ[0]=at+s+1;n->succ[1]=-1;
    }
    dbuf_printf(f->diagnostics,"; wide loop-cursor trial streams=%d trip=%lu index=%d bounded-object=1\n",
      count,(unsigned long)bound,index);
    ++changed;
  }
  if(changed)wide_reset_allocation(f);
  return changed;
}
