/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retain DR16 = (base + constant) mod 2^24 between ordinary memory nodes.
 * Observe the actual emitted writes, including scratch used by pure MIR.
 * Each memory transaction and each pointer normalization remain in place. */
static void
wrelative_observe(wide_function *f,const char *line,size_t length)
{
  if(!f->relativeAddressValid || !length || line[0]==';')return;
  if(line[0]!='\t' || line[1]=='j' || !strncmp(line+1,"djnz",4) ||
     !strncmp(line+1,"ret",3) || !strncmp(line+1,"eret",4)) {
    f->relativeAddressValid=false;return;
  }
  mx251_effects effect=mx251InstructionEffects(line,length,line,length);
  /* A known ordinary direct-object load writes only its generated GPR
   * destination. The shared instruction API deliberately lacks symbols. */
  if(effect.unknown && f->scratchNode>=0 && f->scratchNode<f->nn) {
    wide_node *n=&f->nodes[f->scratchNode];
    if(n->op==WM_SYMBOL_LOAD && !n->byteAccess && !n->argumentReadReverse &&
       n->width>1 && n->addressSymbol && n->result>=0) {
      int reg=f->values[n->result].reg;if(reg<0)reg=20;
      char expected[256];int size=SNPRINTF(expected,sizeof expected,"\tmov\t%s%d,(%s%+d)",
        n->width==4?"dr":"wr",reg,n->addressSymbol->rname,n->addressOffset);
      if(size>=0 && (size_t)size==length && !memcmp(line,expected,length))
        effect=(mx251_effects){.writes=wide_mask(reg,n->width)};
    }
  }
  uint32_t live=wide_mask(16,4)|wide_mask(f->values[f->relativeAddressBase].reg,4);
  if(effect.unknown || effect.calls || (effect.memory&MX251_MEMORY_WRITE) || (effect.writes&live))
    f->relativeAddressValid=false;
}

static bool
wrelative_path(wide_function *f,int first,int last)
{
  if(last<=first)return false;
  for(int i=first;i<=last;++i) {
    wide_node *n=&f->nodes[i];
    if(n->orderedReadCount || n->orderedAsmCount || n->memoryStateBarrier ||
       (i>first && (n->op==LABEL || n->op==CALL || n->op==PCALL)) ||
       (i<last && (n->succ[0]!=i+1 || n->succ[1]>=0 || n->jumpCount)))return false;
  }
  return true;
}

static bool
wrelative_candidate(wide_function *f,wide_node *n,bool ready,int64_t *offset)
{
  const char *enabled=getenv("MX251_RELATIVE_ADDRESS");
  if((enabled && !strcmp(enabled,"0")) || ready || f->scratchNode<0 ||
     n!=&f->nodes[f->scratchNode] || (n->op!=WM_LOAD && n->op!=WM_STORE) ||
     (n->addressOp!='+' && n->addressOp!='-') || n->nativeIndexed ||
     n->addressObject>=0 || n->addressSymbol || n->byteAccess || n->codeAccess || n->pointerAccess ||
     n->left.value<0 || n->left.width!=4 || n->left.codePointer ||
     (n->width!=1 && n->width!=2 && n->width!=4) ||
     n->addressIndex.value>=0 || n->addressIndex.floating || n->addressIndex.codePointer ||
     (n->addressIndex.width!=1 && n->addressIndex.width!=2 && n->addressIndex.width!=4))return false;
  wide_value *base=&f->values[n->left.value];
  if(!base->pointer || base->codePointer || base->symbolSnapshot || base->reg<0 ||
     base->width!=4 || base->sym->addrtaken || isVolatile(base->sym->type) || isAtomic(base->sym->type) ||
     (wide_mask(base->reg,4)&(wide_mask(16,4)|wide_mask(20,4)|MCS251_AB_BYTES)))return false;
  if(n->op==WM_STORE) {
    if(n->right.width!=n->width || n->right.floating || n->result>=0 || !wb_empty(n->def))return false;
  } else if(n->result<0 || f->values[n->result].pointer)return false;
  unsigned bits=8*n->addressIndex.width;uint32_t raw=n->addressIndex.literal;
  if(bits<32)raw&=(UINT32_C(1)<<bits)-1;
  int64_t value=raw;
  if(n->addressIndex.sign && (raw&(UINT32_C(1)<<(bits-1))))value-=INT64_C(1)<<bits;
  *offset=n->addressOp=='-'?-value:value;
  return true;
}
