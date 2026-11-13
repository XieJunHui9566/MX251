/* Assemble disjoint unsigned bytes without widening each intermediate.
 * Memory nodes stay in place: the pack consumes their sampled byte values.
 * GPL-2.0-or-later. */
typedef struct {
  wide_operand lanes[4];
  bool remove[WN_MAX];
  unsigned occupied, inputs, operations;
  int root, first, width;
} wide_byte_pack;

static bool
wide_pack_lane(wide_byte_pack *p,wide_operand value,unsigned shift)
{
  if(shift>=(unsigned)p->width*8 || (p->occupied&(1u<<(shift/8))))return false;
  p->lanes[p->width-1-shift/8]=value;p->occupied|=1u<<(shift/8);
  p->inputs+=value.value>=0;return true;
}

static bool
wide_pack_operand(wide_function *f,wide_byte_pack *p,wide_operand o,int before,
                  unsigned shift,const int *definition,const unsigned *uses)
{
  if(o.floating || o.codePointer || (o.width!=1 && o.width!=2 && o.width!=4) ||
     shift>=(unsigned)p->width*8)return false;
  if(o.value<0) {
    uint32_t value=o.literal;
    if(o.sign && o.width<4 && (value&(UINT32_C(1)<<(8*o.width-1))))
      value|=~((UINT32_C(1)<<(8*o.width))-1);
    value<<=shift;
    for(int k=0;k<p->width;++k)if((value>>(8*k))&255)
      if(!wide_pack_lane(p,(wide_operand){.value=-1,.width=1,.literal=(value>>(8*k))&255},8*k))return false;
    return true;
  }
  wide_value *v=&f->values[o.value];
  if(v->floating || v->pointer)return false;
  if(o.width==1 && v->width==1 && !o.sign) {
    /* Preserve the snapshot used by the original expression, including a
     * volatile byte load. Later source-variable assignments cannot replace it. */
    for(int i=before;i<p->root;++i)if(wb_has(f->nodes[i].def,o.value))return false;
    return wide_pack_lane(p,o,shift);
  }
  if(o.width!=p->width || v->width!=p->width)return false;
  int at=definition[o.value];
  if(at<p->first || at>=before)return false;
  wide_node *n=&f->nodes[at];
  if(n->width!=p->width || n->floating || n->orderedReadCount || n->memoryStateBarrier)return false;
  /* Parameter/cast folding may already have widened a physical byte. Keep
   * that original sampling node and consume its known unsigned low byte. */
  if(n->op==WM_PARAM_LOAD && n->left.width==1 && !n->left.sign) {
    o.width=1;o.sign=false;
    return wide_pack_lane(p,o,shift);
  }
  if(!p->remove[at])++p->operations;
  p->remove[at]=true;
  switch(n->op) {
  case '=':case CAST:
    if(n->right.width!=p->width && (n->right.width!=1 || n->right.sign))return false;
    return wide_pack_operand(f,p,n->right,at,shift,definition,uses);
  case LEFT_OP:
    if(n->right.value>=0 || n->right.literal%8 || n->right.literal>=(unsigned)p->width*8)return false;
    return wide_pack_operand(f,p,n->left,at,shift+n->right.literal,definition,uses);
  case '+':case '|':case '^':
    return wide_pack_operand(f,p,n->left,at,shift,definition,uses) &&
           wide_pack_operand(f,p,n->right,at,shift,definition,uses);
  default:return false;
  }
}

static unsigned
wide_byte_packs(wide_function *f)
{
  const char *enabled=getenv("MX251_BYTE_PACK");
  if(enabled && !strcmp(enabled,"0"))return 0;
  int definition[WV_MAX];unsigned uses[WV_MAX]={0},changes=0;
  for(int v=0;v<f->nv;++v)definition[v]=-1;
  for(int i=0;i<f->nn;++i)for(int v=0;v<f->nv;++v) {
    if(wb_has(f->nodes[i].def,v))definition[v]=definition[v]==-1?i:-2;
    uses[v]+=wb_has(f->nodes[i].use,v);
  }
  for(int at=f->nn-1;at>=0;--at) {
    wide_node *n=&f->nodes[at];
    if((n->op!='+' && n->op!='|' && n->op!='^') || n->result<0 ||
       (n->width!=2 && n->width!=4) || n->floating ||
       f->values[n->result].pointer || n->orderedReadCount)continue;
    wide_byte_pack p={.root=at,.first=at,.width=n->width};
    for(int k=0;k<4;++k)p.lanes[k]=(wide_operand){.value=-1,.width=1};
    while(p.first>0) {
      int i=p.first-1;wide_node *previous=&f->nodes[i];
      if(previous->op==LABEL || previous->op==CALL || previous->op==PCALL ||
         previous->op==WM_ORDERED_ASM || previous->memoryStateBarrier ||
         previous->succ[0]!=i+1 || previous->succ[1]>=0 || previous->jumpCount)break;
      --p.first;
    }
    if(!wide_pack_operand(f,&p,n->left,at,0,definition,uses) ||
       !wide_pack_operand(f,&p,n->right,at,0,definition,uses) ||
       p.inputs<2 || !p.operations)continue;
    bool closed=true;
    for(int i=p.first;i<at && closed;++i)if(p.remove[i] && uses[f->nodes[i].result]>1)
      for(int j=0;j<f->nn;++j)if(j!=at && !p.remove[j] && wb_has(f->nodes[j].use,f->nodes[i].result))closed=false;
    if(!closed)continue;
    for(int i=p.first;i<at;++i)if(p.remove[i]) {
      wide_node *old=&f->nodes[i];old->op=WM_NOP;
      old->result=old->left.value=old->right.value=old->addressIndex.value=-1;
      old->use=old->def=(wide_bits){{0}};
    }
    n->op=WM_BYTE_PACK;n->use=(wide_bits){{0}};
    n->left.value=n->right.value=n->addressIndex.value=-1;
    for(int k=0;k<n->width;++k) {
      n->byteLanes[k]=p.lanes[k];
      if(p.lanes[k].value>=0)wb_set(&n->use,p.lanes[k].value);
    }
    dbuf_printf(f->diagnostics,"; wide byte-pack trial node=%d width=%d inputs=%u removed=%u\n",at,n->width,p.inputs,p.operations);
    ++changes;
  }
  if(changes)wide_reset_allocation(f);
  return changes;
}
