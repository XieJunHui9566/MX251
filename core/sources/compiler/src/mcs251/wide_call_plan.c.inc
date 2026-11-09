/* Machine call constraints and call-spill placement. GPL-2.0-or-later. */
static void
wide_prepare_calls(wide_function *f)
{
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op!=CALL)continue;
    wide_call_contract *p=&n->call;memset(p,0,sizeof *p);
    p->calleeWrites=UINT32_MAX;p->known=wide_leaf_writes(n->callee,&p->calleeWrites);
    p->scratchWrites=MCS251_AB_BYTES|wide_mask(16,4)|wide_mask(20,4);
    if(n->args) {
      p->argumentBytes=getSize(FUNC_ARGS(n->callee->type)->type);
      if(n->ordinaryCall) {
        if(mcs251InternalArgument(n->callee))p->argumentWrites=wide_mask(4,4);
        else if(p->argumentBytes>2)p->argumentWrites|=wide_mask(10,p->argumentBytes==4?2:1);
      } else {
        p->argumentWrites=wide_mask(8-n->left.width,n->left.width);
        if(n->args>1)p->argumentWrites|=wide_mask(16-n->right.width,n->right.width);
      }
    }
    if(n->result>=0) {
      p->resultBytes=getSize(n->callee->type->next);
      if(!n->ordinaryCall || mcs251InternalReturn(n->callee))
        p->resultReads=wide_mask(n->width==1?7:8-n->width,n->width);
      else if(p->resultBytes>2)p->resultReads=wide_mask(10,p->resultBytes==4?2:1);
    }
  }
}

static uint32_t
wide_call_writes(wide_function *f,wide_node *n)
{
  uint32_t writes=n->call.calleeWrites|n->call.argumentWrites|n->call.scratchWrites|n->call.resultReads;
  if(n->result>=0 && f->values[n->result].reg>=0)
    writes|=wide_mask(f->values[n->result].reg,f->values[n->result].width);
  return writes;
}

static bool
wide_call_block_end(wide_function *f,int i)
{
  wide_node *n=&f->nodes[i];
  return i+1==f->nn || n->succ[0]!=i+1 || n->succ[1]>=0 || n->jumpCount ||
    f->nodes[i+1].op==LABEL || n->op==RETURN || n->op==ENDFUNCTION;
}

/* A value's private stack copy remains valid until its next definition.
 * Registers are restored at a use, or before a CFG edge. The plan therefore
 * needs no path-dependent register state at joins and loop back edges. */
static bool
wide_plan_call_frame(wide_function *f)
{
  unsigned hits[WV_MAX]={0},best[WV_MAX]={0};wide_bits selected={{0}};
  unsigned calls=0;
  for(int i=0;i<f->nn;++i)calls+=f->nodes[i].op==CALL;
  if(calls<2)return false;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->rotateCandidate)return false;
    for(int v=0;v<f->nv;++v) {
      if(wb_has(n->def,v))hits[v]=0;
      if(n->op==CALL && !wide_call_block_end(f,i) && v!=n->result &&
         f->values[v].reg>=0 && wb_has(n->out,v) &&
         (wide_call_writes(f,n)&wide_mask(f->values[v].reg,f->values[v].width))) {
        if(++hits[v]>best[v])best[v]=hits[v];
      }
      if(wide_call_block_end(f,i))hits[v]=0;
    }
  }
  int frame=f->frame;
  for(int v=0;v<f->nv;++v) {
    f->callSlots[v]=-1;
    if(best[v]<2)continue;
    wb_set(&selected,v);f->callSlots[v]=frame;frame+=f->values[v].width;
  }
  if(wb_empty(selected) || frame>512)return false;
  wide_bits stored={{0}},pending={{0}};
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];bool boundary=wide_call_block_end(f,i);
    n->callFrameValues=n->callStores=n->callLoads=n->callArguments=(wide_bits){{0}};
    for(int v=0;v<f->nv;++v)if(wb_has(selected,v)) {
      if(!wb_has(pending,v))continue;
      /* A call can consume the private copy without restoring its register.
       * At an outgoing CFG edge, live values still use the canonical register. */
      if(n->op==CALL && wb_has(n->use,v) && (!boundary || !wb_has(n->out,v)))
        wb_set(&n->callArguments,v);
      else if(wb_has(n->use,v) || (boundary && wb_has(n->in,v)))
        wb_set(&n->callLoads,v);
    }
    pending=wb_minus(pending,wb_or(n->callLoads,n->def));
    stored=wb_minus(stored,n->def);
    if(n->op==CALL && !boundary)for(int v=0;v<f->nv;++v) {
      if(!wb_has(selected,v) || v==n->result || !wb_has(n->out,v) ||
         !(wide_call_writes(f,n)&wide_mask(f->values[v].reg,f->values[v].width)))continue;
      wb_set(&n->callFrameValues,v);
      if(!wb_has(stored,v))wb_set(&n->callStores,v);
      wb_set(&stored,v);wb_set(&pending,v);
    }
    for(int w=0;w<WB_WORDS;++w) {pending.w[w]&=n->out.w[w];stored.w[w]&=n->out.w[w];}
    if(boundary)stored=pending=(wide_bits){{0}};
  }
  /* Account for the actual transport instructions, independently per value.
   * A dword uses two stack MOVs, whereas PUSH/POP DR each use one. */
  frame=f->frame;
  for(int v=0;v<f->nv;++v)if(wb_has(selected,v)) {
    unsigned before=0,after=0,width=f->values[v].width;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];unsigned weight=1+4*n->origin->depth;
      before+=wb_has(n->callFrameValues,v)*2*weight;
      after+=(wb_has(n->callLoads,v)*(width==4?2:1)+
              wb_has(n->callStores,v)*(width==2?1:2))*weight;
    }
    if(after>=before) {
      wide_bits remove={{0}};wb_set(&remove,v);f->callSlots[v]=-1;
      for(int i=0;i<f->nn;++i) {
        wide_node *n=&f->nodes[i];
        n->callFrameValues=wb_minus(n->callFrameValues,remove);
        n->callLoads=wb_minus(n->callLoads,remove);n->callStores=wb_minus(n->callStores,remove);
        n->callArguments=wb_minus(n->callArguments,remove);
      }
    } else {f->callSlots[v]=frame;frame+=width;}
  }
  if(frame==f->frame)return false;
  f->frame=frame;f->callFrame=true;return true;
}

static bool
wide_call_plain(wide_function *f,int first,int count)
{
  if(!f->callFrame)return true;
  for(int i=first;i<first+count && i<f->nn;++i)
    if(!wb_empty(f->nodes[i].callLoads) || !wb_empty(f->nodes[i].callStores))return false;
  return true;
}
