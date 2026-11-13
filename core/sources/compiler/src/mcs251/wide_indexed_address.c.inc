/* SPDX-License-Identifier: GPL-2.0-or-later
 * Build a dynamic offset in the address scratch and add the resident base.
 * The integer scale keeps its original width before pointer extension. */
static void wmemory(wide_function *,wide_node *,bool);

/* Allocated C objects lie wholly in the target's 24-bit address domain. A
 * defined dereference of an object-derived pointer lies in that same object;
 * its full-width sum therefore already has zero padding. Do not infer this
 * from a pointer's type alone, or from a mutable/loaded/absolute address. */
static bool
windexed_object_base(wide_function *f,int value,unsigned depth)
{
  if(value<0 || depth>16 || !f->values[value].pointer ||
     f->values[value].width!=4 || f->values[value].symbolSnapshot)return false;
  wide_node *definition=NULL;
  for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].def,value)) {
    if(definition)return false;definition=&f->nodes[i];
  }
  if(!definition)return false;
  wide_node *n=definition;
  if(n->op==WM_ADDRESS && n->addressSymbol) {
    symbol *s=n->addressSymbol;sym_link *spec=getSpec(s->type);
    return !SPEC_ABSA(spec) && !s->_isparm && getSize(s->type)>0 &&
      SPEC_SCLS(spec)!=S_SFR && SPEC_SCLS(spec)!=S_SBIT;
  }
  if((n->op=='=' || n->op==CAST) && n->right.width==4)
    return windexed_object_base(f,n->right.value,depth+1);
  if((n->op=='+' || n->op=='-') && n->left.width==4)
    return windexed_object_base(f,n->left.value,depth+1);
  return false;
}

static bool
windexed_object_access(wide_function *f,wide_node *n)
{
  return (n->op==WM_LOAD || n->op==WM_STORE) && !n->byteAccess &&
    !n->pointerAccess && n->width>0 && windexed_object_base(f,n->left.value,0);
}

static bool
windexed_address_enabled(void)
{
  const char *option=getenv("MX251_INDEXED_ADDRESS");
  return !option || strcmp(option,"0");
}

static int
windexed_base(wide_function *f,wide_node *n)
{
  if(!windexed_address_enabled() || n->addressOp!='+' || n->left.value<0 ||
     n->left.width!=4 || n->addressIndex.value<0 || n->addressIndex.floating ||
     n->addressIndex.codePointer)return -1;
  int base=wsource_location(f,n->left.value).reg;
  /* Offset sign extension may use A/B. DR16/20 belong to the emitter. */
  if(base<0 || (wide_mask(base,4)&(MCS251_AB_BYTES|wide_mask(16,4)|wide_mask(20,4))))return -1;
  return base;
}

static bool
windexed_offset_first(wide_function *f,wide_node *n)
{
  int base=windexed_base(f,n);
  if(base<0 || n->addressIndex.width>=4)return false;
  we(f,"; wide indexed offset-first width=%d base=%d\n",n->addressIndex.width,base);
  wload(f,n->addressIndex,16,4);
  we(f,"\tadd\tdr16,dr%d\n",base);
  return true;
}

static int
windexed_address(wide_function *f,wide_node *n)
{
  if(!n->addressOp)return wread(f,n->left,16,4);
  we(f,"; wide indexed address op=%c index-width=%d\n",n->addressOp,n->addressIndex.width);
  if(!windexed_offset_first(f,n)) {
    wload(f,n->left,16,4);
    wide_operand offset=n->addressIndex;uint32_t literal=offset.literal;
    if(offset.sign && offset.width<4 && (literal&(UINT32_C(1)<<(8*offset.width-1))))
      literal|=~((UINT32_C(1)<<(8*offset.width))-1);
    if(offset.value<0 && literal<=0xffff) {
      wstep(f,16,4,n->addressOp=='-',literal);
    } else {int index=wread(f,offset,20,4);we(f,"\t%s\tdr16,dr%d\n",n->addressOp=='+'?"add":"sub",index);}
  }
  if(!windexed_object_access(f,n))we(f,"\tanl\twr16,#0x00ff\n");
  else we(f,"; wide object address padding=zero\n");
  return 16;
}

static int
windexed_scaled_memory(wide_function *f,int first)
{
  wide_node *scale=&f->nodes[first];wide_operand input=scale->left,constant=scale->right;
  if(!windexed_address_enabled() || scale->result<0 || scale->width!=2 ||
     scale->sign || scale->floating || f->values[scale->result].reads!=1)return first;
  unsigned shifts=0;
  if(scale->op==WM_UMULW) {
    if(input.value<0) {wide_operand t=input;input=constant;constant=t;}
    if(constant.value<0 && (constant.literal==2 || constant.literal==4))
      shifts=constant.literal==2?1:2;
  } else if(scale->op==LEFT_OP && constant.value<0 && (constant.literal==1 || constant.literal==2))
    shifts=constant.literal;
  if(!shifts || input.value<0 || input.sign || input.floating || input.codePointer ||
     (input.width!=1 && input.width!=2))return first;
  int next=first+1;
  while(next<f->nn && f->nodes[next].op==WM_NOP)++next;
  if(next==f->nn || !wide_call_plain(f,first+1,next-first))return first;
  for(int i=first;i<next;++i)
    if(f->nodes[i].succ[0]!=i+1 || f->nodes[i].succ[1]>=0 || f->nodes[i].jumpCount)return first;
  wide_node *memory=&f->nodes[next];
  if((memory->op!=WM_LOAD && memory->op!=WM_STORE) || memory->byteAccess || memory->pointerAccess ||
     memory->addressObject>=0 || memory->addressIndex.value!=scale->result ||
     memory->addressIndex.width!=2 || memory->addressIndex.sign ||
     memory->left.value==scale->result || memory->right.value==scale->result ||
     wb_has(memory->out,scale->result) ||
     (memory->codeAccess && !mcs251FlatCodeReadsEnabled))return first;
  int base=windexed_base(f,memory);
  if(base<0)return first;
  we(f,"; wide indexed scale fused shift=%u integer-width=16 base=%d\n",shifts,base);
  wload(f,input,18,2);
  for(unsigned k=0;k<shifts;++k)we(f,"\tadd\twr18,wr18\n");
  /* Truncate at 16 bits before extending, as in the unfused integer node. */
  f->emitWeight=1+4*memory->origin->depth;
  we(f,"\tsub\twr16,wr16\n\tadd\tdr16,dr%d\n",base);
  if(!windexed_object_access(f,memory))we(f,"\tanl\twr16,#0x00ff\n");
  else we(f,"; wide object address padding=zero\n");
  f->scratchAddressValid=false;f->scratchNode=next;
  wmemory(f,memory,true);
  return next;
}
