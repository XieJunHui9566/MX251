/* SPDX-License-Identifier: GPL-2.0-or-later
 * Available ordinary memory values, including EDATA and integer subranges.
 * Keep all stores and volatile transactions in place. A constant far store
 * only kills objects whose address space can overlap its physical range.
 */
typedef struct {
  symbol *symbol;
  int object,offset,width;
  wide_operand value;
} wide_memory_fact;

static uint32_t
wide_memory_mask(int width)
{
  return width==4?UINT32_MAX:(UINT32_C(1)<<(8*width))-1;
}

static bool
wide_memory_literal(wide_operand operand,const bool *known,const uint32_t *bits,uint32_t *value)
{
  if(operand.floating || operand.codePointer ||
     (operand.width!=1 && operand.width!=2 && operand.width!=4))return false;
  if(operand.value>=0 && !known[operand.value])return false;
  uint32_t mask=wide_memory_mask(operand.width);
  *value=(operand.value<0?operand.literal:bits[operand.value])&mask;
  if(operand.sign && operand.width<4 && (*value&(UINT32_C(1)<<(8*operand.width-1))))*value|=~mask;
  return true;
}

static bool
wide_memory_constant(wide_function *f,wide_node *n,const bool *known,const uint32_t *bits,uint32_t *value)
{
  if(n->result<0 || n->orderedReadCount || n->orderedAsmCount || n->byteAccess ||
     n->memoryStateBarrier || n->addressSymbol || n->addressObject>=0)return false;
  wide_value *result=&f->values[n->result];
  if(result->pointer || result->codePointer || result->floating ||
     (result->width!=1 && result->width!=2 && result->width!=4))return false;
  uint32_t a,b;
  if(!wide_memory_literal(n->right,known,bits,&b))return false;
  if(n->op=='=' || n->op==CAST)*value=b;
  else {
    if(!wide_memory_literal(n->left,known,bits,&a))return false;
    switch(n->op) {
      case '+': *value=a+b;break;
      case '-': *value=a-b;break;
      case '*': *value=a*b;break;
      case BITWISEAND: *value=a&b;break;
      case '|': *value=a|b;break;
      case '^': *value=a^b;break;
      case LEFT_OP:
        if(b>=(unsigned)n->left.width*8)return false;
        *value=a<<b;break;
      case RIGHT_OP:
        if(n->left.sign || b>=(unsigned)n->left.width*8)return false;
        *value=a>>b;break;
      case GETABIT:case GETBYTE:case GETWORD:
        if(b>=(unsigned)n->left.width*8)return false;
        *value=(a>>b)&(n->op==GETABIT?1u:n->op==GETBYTE?255u:65535u);break;
      default:return false;
    }
  }
  *value&=wide_memory_mask(result->width);return true;
}

static bool
wide_memory_direct(wide_node *n,bool store)
{
  return store?(n->op==WM_SYMBOL_STORE || n->op==WM_OBJECT_STORE || n->op==WM_EDATA_STORE):
    (n->op==WM_SYMBOL_LOAD || n->op==WM_OBJECT_LOAD || n->op==WM_EDATA_LOAD);
}

static bool
wide_memory_ordinary(wide_node *n)
{
  symbol *s=n->addressSymbol;
  return s && !n->byteAccess && !n->pointerAccess && !n->codeAccess &&
    !n->addressOp && !n->orderedReadCount && !n->orderedAsmCount &&
    !isVolatile(s->type) && !isAtomic(s->type) && !SPEC_ABSA(s->etype) &&
    SPEC_OCLS(s->etype)!=sfr && n->addressOffset>=0 &&
    (n->width==1 || n->width==2 || n->width==4);
}

static bool
wide_memory_address_operand(wide_operand operand,const bool *known,const uint32_t *bits,int64_t *address)
{
  if(operand.width!=4 || operand.floating || operand.codePointer ||
     (operand.value>=0 && !known[operand.value]))return false;
  *address=operand.value<0?operand.literal:bits[operand.value];
  return *address<=0xffffff;
}

static bool
wide_memory_address(wide_node *n,const bool *known,const uint32_t *bits,uint32_t *value)
{
  int64_t address,delta;
  if(n->addressObject>=0)return false;
  if(n->addressSymbol) {
    symbol *s=n->addressSymbol;
    if(!SPEC_ABSA(s->etype) || s->onStack || s->_isparm)return false;
    address=(int64_t)SPEC_ADDR(s->etype)+n->addressOffset;
  } else if(!wide_memory_address_operand(n->left,known,bits,&address))return false;
  if(n->addressOp) {
    if((n->addressOp!='+' && n->addressOp!='-') ||
       !wide_fixed_address_delta(n->addressIndex,&delta))return false;
    address+=n->addressOp=='+'?delta:-delta;
  }
  if(address<0 || address>0xffffff)return false;
  *value=(uint32_t)address;return true;
}

static bool
wide_memory_far_disjoint(wide_memory_fact *fact,wide_node *store,const bool *known,const uint32_t *bits)
{
  uint32_t address;
  if((store->op!=WM_STORE && !wide_memory_direct(store,true)) || store->width<=0 ||
     !wide_memory_address(store,known,bits,&address) ||
     address<65536 || address>UINT32_C(0x1000000)-(unsigned)store->width)return false;
  symbol *s=fact->symbol;
  if(!s || s->onStack || s->_isparm || SPEC_ABSA(s->etype))return false;
  memmap *map=SPEC_OCLS(s->etype);
  /* EDATA's 16-bit region and DATA/IDATA's byte region cannot overlap a
   * non-wrapping far write above region 00. No peripheral address list. */
  return map==edata || map==data || map==idata;
}

static unsigned
wide_memory_immediates(wide_node *n,const bool *known,const uint32_t *bits)
{
  if(n->orderedReadCount || n->orderedAsmCount || n->memoryStateBarrier)return 0;
  bool binary=n->op=='+' || n->op=='-' || n->op=='*' || n->op==BITWISEAND ||
    n->op=='|' || n->op=='^' || n->op==LEFT_OP || n->op==RIGHT_OP;
  bool right=binary || n->op=='=' || n->op==CAST || wide_memory_direct(n,true) || n->op==WM_STORE;
  unsigned changes=0;
  wide_operand *operands[2]={&n->left,&n->right};
  for(int k=0;k<2;++k) {
    wide_operand *o=operands[k];uint32_t value;int previous=o->value;
    if(!(k?right:binary) || previous<0 || !wide_memory_literal(*o,known,bits,&value))continue;
    o->value=-1;o->literal=value&wide_memory_mask(o->width);++changes;
    bool used=n->left.value==previous || n->right.value==previous || n->addressIndex.value==previous;
    for(unsigned j=0;j<n->stackArgs;++j)used|=n->stackOperands[j].value==previous;
    if(n->op==WM_BYTE_PACK)for(int j=0;j<n->width;++j)used|=n->byteLanes[j].value==previous;
    if(!used) {wide_bits one={{0}};wb_set(&one,previous);n->use=wb_minus(n->use,one);}
  }
  return changes;
}

static unsigned
wide_memory_values(wide_function *f)
{
  const char *enabled=getenv("MX251_MEMORY_VALUES");
  if(enabled && !strcmp(enabled,"0"))return 0;
  wide_memory_fact facts[WN_MAX];int count=0;
  bool known[WV_MAX]={0};uint32_t bits[WV_MAX]={0};
  bool addressKnown[WV_MAX]={0};uint32_t addressBits[WV_MAX]={0};
  bool copies[WN_MAX]={0};
  unsigned loads=0,constants=0,subranges=0,preserved=0,immediates=0,removed=0;
  for(int at=0;at<f->nn;++at) {
    wide_node *n=&f->nodes[at];
    bool boundary=wide_sink_block_barrier(n) || n->op==CALL || n->op==PCALL ||
      n->orderedReadCount || n->orderedAsmCount || n->memoryStateBarrier;
    if(boundary){count=0;memset(known,0,sizeof known);memset(addressKnown,0,sizeof addressKnown);}
    immediates+=wide_memory_immediates(n,known,bits);
    /* Read the old definition before killing a destructive assignment. */
    uint32_t constant=0;bool constantResult=wide_memory_constant(f,n,known,bits,&constant);
    uint32_t address=0;bool addressResult=false;
    if(n->result>=0 && f->values[n->result].pointer && !f->values[n->result].codePointer &&
       !n->orderedReadCount && !n->orderedAsmCount && !n->memoryStateBarrier) {
      if(n->op==WM_ADDRESS)addressResult=wide_memory_address(n,addressKnown,addressBits,&address);
      else if(n->op=='=' || n->op==CAST) {
        int64_t value;
        addressResult=wide_memory_address_operand(n->right,addressKnown,addressBits,&value);
        if(addressResult)address=(uint32_t)value;
      } else if(n->op=='+' || n->op=='-') {
        int64_t value,delta;
        if(wide_memory_address_operand(n->left,addressKnown,addressBits,&value) &&
           wide_fixed_address_delta(n->right,&delta)) {
          value+=n->op=='+'?delta:-delta;
          addressResult=value>=0 && value<=0xffffff;
          if(addressResult)address=(uint32_t)value;
        }
      }
    }
    for(int v=0;v<f->nv;++v)if(wb_has(n->def,v)) {
      known[v]=addressKnown[v]=false;
      for(int k=0;k<count;++k)if(facts[k].value.value==v)facts[k].symbol=NULL;
    }
    bool load=wide_memory_direct(n,false),store=wide_memory_direct(n,true);
    bool ordinary=(load || store) && wide_memory_ordinary(n);
    if(ordinary && load) {
      int found=-1;
      for(int k=0;k<count;++k) {
        wide_memory_fact *p=&facts[k];
        if(p->symbol!=n->addressSymbol || p->object!=n->addressObject ||
           p->offset>n->addressOffset || p->offset+p->width<n->addressOffset+n->width ||
           p->value.width!=p->width || p->value.codePointer || p->value.floating!=n->floating)continue;
        bool full=p->offset==n->addressOffset && p->width==n->width;
        bool low=p->offset+p->width==n->addressOffset+n->width;
        if(n->floating?full && p->value.sign==n->sign:
           (full || low || p->value.value<0)) {found=k;break;}
      }
      if(found>=0 && n->result>=0) {
        wide_memory_fact fact=facts[found];wide_operand value=fact.value;
        bool full=fact.offset==n->addressOffset && fact.width==n->width;
        if(value.value<0 && !value.floating) {
          unsigned shift=8*(fact.offset+fact.width-n->addressOffset-n->width);
          value.literal=(value.literal>>shift)&wide_memory_mask(n->width);
          value.width=n->width;value.sign=n->sign;
          constantResult=true;constant=value.literal;
        }
        n->op=full?'=':CAST;n->right=value;n->left=(wide_operand){.value=-1};
        n->use=(wide_bits){{0}};wb_set(&n->use,value.value);
        n->addressSymbol=NULL;n->addressObject=-1;n->addressOffset=0;
        f->values[n->result].symbolSnapshot=false;
        copies[at]=true;
        ++loads;subranges+=!full;
      } else if(count<WN_MAX) {
        facts[count++]=(wide_memory_fact){n->addressSymbol,n->addressObject,n->addressOffset,n->width,
          {.value=n->result,.width=n->width,.sign=n->sign,.floating=n->floating}};
      }
    } else if(store || n->op==WM_STORE || n->op==WM_PARAM_STORE ||
              n->op==WM_FIELD_STORE || n->op==WM_BIT_STORE) {
      for(int k=0;k<count;++k)if(facts[k].symbol) {
        wide_memory_fact *p=&facts[k];
        bool independent=ordinary && p->symbol!=n->addressSymbol;
        if(ordinary && p->symbol==n->addressSymbol && p->object==n->addressObject)
          independent=p->offset>=n->addressOffset+n->width || n->addressOffset>=p->offset+p->width;
        if(independent)continue;
        if(wide_memory_far_disjoint(p,n,addressKnown,addressBits)){++preserved;continue;}
        p->symbol=NULL;
      }
      if(ordinary && count<WN_MAX) {
        wide_operand value=n->right;uint32_t literal;
        if(!n->floating && wide_memory_literal(value,known,bits,&literal))
          value=(wide_operand){.value=-1,.width=n->width,.sign=n->sign,.literal=literal&wide_memory_mask(n->width)};
        if(value.width==n->width && !value.codePointer && value.floating==n->floating)
          facts[count++]=(wide_memory_fact){n->addressSymbol,n->addressObject,n->addressOffset,n->width,value};
      }
    }
    if(constantResult) {
      int result=n->result;known[result]=true;bits[result]=constant;
      copies[at]=true;
      if(n->op!='=' || n->right.value>=0 || n->right.literal!=constant) {
        n->op='=';n->right=(wide_operand){.value=-1,.width=f->values[result].width,
          .sign=f->values[result].sign,.literal=constant};
        n->left=(wide_operand){.value=-1};n->use=(wide_bits){{0}};++constants;
      }
    }
    if(addressResult){addressKnown[n->result]=true;addressBits[n->result]=address;}
    if(n->succ[0]!=at+1 || n->succ[1]>=0 || n->jumpCount) {
      count=0;memset(known,0,sizeof known);memset(addressKnown,0,sizeof addressKnown);
    }
  }
  if(loads || constants || immediates) {
    bool changed;
    do {
      wide_reset_allocation(f);wide_liveness(f);changed=false;
      for(int i=0;i<f->nn;++i) {
        wide_node *n=&f->nodes[i];
        if(!copies[i] || n->result<0 || wb_has(n->out,n->result))continue;
        n->op=WM_NOP;n->result=n->left.value=n->right.value=n->addressIndex.value=-1;
        n->use=n->def=(wide_bits){{0}};changed=true;++removed;
      }
    } while(changed);
    wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide memory-values loads=%u subranges=%u constants=%u disjoint-writes=%u immediates=%u removed=%u\n",
      loads,subranges,constants,preserved,immediates,removed);
  }
  return loads+constants+immediates+removed;
}
