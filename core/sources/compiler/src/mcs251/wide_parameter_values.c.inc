/* SPDX-License-Identifier: GPL-2.0-or-later
 * A by-value stack parameter whose address never escapes can live in a MIR
 * value. Load its incoming ABI slot once, then let allocation place updates.
 */
static unsigned
wide_parameter_values(wide_function *f)
{
  int entry=-1,insert=-1;
  for(int i=0;i<f->nn;++i) {
    if(f->nodes[i].op==FUNCTION)entry=i;
    if(f->nodes[i].op==RECEIVE)insert=i+1;
  }
  if(entry<0)return 0;
  if(insert<0)insert=entry+1;
  /* Consume register arguments before an incoming stack load uses A/B.
   * The original body, including any loop entry label, follows initialization. */
  for(int i=entry;i<insert;++i)
    if(f->nodes[i].succ[0]!=i+1 || f->nodes[i].succ[1]>=0 || f->nodes[i].jumpCount)return 0;
  wide_node initial[WA_MAX];unsigned count=0,index=0;int offset=-2;
  for(value *arg=FUNC_ARGS(currFunc->type);arg;arg=arg->next,++index) {
    if(index<wide_register_count(currFunc->type))continue;
    sym_link *type=arg->type;symbol *s=arg->sym;int physical=getSize(type);
    offset-=physical;
    if(!s || !s->onStack || !s->_isparm ||
       isVolatile(type) || isAtomic(type) || !wide_abi_type(type,currFunc->type))continue;
    int width=wide_width(type);bool floating=IS_FLOAT(type),pointer=wide_pointer(type);
    bool sign=!floating && !pointer && !SPEC_USIGN(getSpec(type));
    unsigned loads=0,stores=0;bool valid=true;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->op==WM_PARAM_ADDRESS && n->addressSymbol==s) {valid=false;break;}
      if((n->op!=WM_PARAM_LOAD && n->op!=WM_PARAM_STORE) || n->addressOffset!=offset)continue;
      if(i<insert || n->left.width!=physical || n->left.sign!=sign ||
         n->floating!=floating || n->byteAccess || n->orderedReadCount ||
         n->addressOp || n->addressObject>=0 || (n->addressSymbol && n->addressSymbol!=s)) {valid=false;break;}
      if(n->op==WM_PARAM_STORE) {
        if(n->width!=width || n->right.width!=width) {valid=false;break;}
        ++stores;
      } else ++loads;
    }
    if(!valid || !loads || loads+stores<2 || count==WA_MAX ||
       f->nv==f->valueLimit || f->nn+(int)count+1>f->nodeLimit)continue;
    int home=f->nv++;
    f->values[home]=(wide_value){.sym=s,.width=width,.sign=sign,.pointer=pointer,
      .floating=floating,.codePointer=wide_code_pointer(type),.symbolSnapshot=true,
      .reg=-1,.slot=-1,.pin=-1,.preference=-1};
    wide_operand input={.value=home,.width=width,.sign=sign,.floating=floating,
      .codePointer=wide_code_pointer(type)};
    initial[count]=(wide_node){.op=WM_PARAM_LOAD,.origin=f->nodes[entry].origin,
      .result=home,.width=width,.sign=sign,.floating=floating,
      .addressOffset=offset,.addressObject=-1,.label=-1,.target=-1,.succ={-1,-1},
      .left={.value=-1,.width=physical,.sign=sign},.right={.value=-1},.addressIndex={.value=-1}};
    wb_set(&initial[count].def,home);
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if((n->op!=WM_PARAM_LOAD && n->op!=WM_PARAM_STORE) || n->addressOffset!=offset)continue;
      if(n->op==WM_PARAM_LOAD) {
        n->op=CAST;n->right=input;n->use=(wide_bits){{0}};wb_set(&n->use,home);
        /* This result now comes from a value copy, not a memory sample. */
        f->values[n->result].symbolSnapshot=false;
      } else {
        n->op='=';n->result=home;n->def=(wide_bits){{0}};wb_set(&n->def,home);
      }
      n->left=(wide_operand){.value=-1};n->addressOffset=0;n->addressSymbol=NULL;
      n->pointerAccess=false;
    }
    dbuf_printf(f->diagnostics,"; wide parameter-value ordinal=%u physical=%d logical=%d loads=%u stores=%u value=%d\n",
      index+1,physical,width,loads,stores,home);
    ++count;
  }
  if(!count)return 0;
  /* Every old branch retains its original target. Only the entry fallthrough
   * executes the new loads, so a backedge to the first label skips them. */
  int oldCount=f->nn;
  memmove(&f->nodes[insert+count],&f->nodes[insert],(oldCount-insert)*sizeof f->nodes[0]);
  f->nn+=(int)count;
  for(int i=0;i<f->nn;++i) {
    if(i>=insert && i<insert+(int)count)continue;
    wide_node *n=&f->nodes[i];
    for(int k=0;k<2;++k)if(n->succ[k]>=insert)
      if(!(i==insert-1 && k==0 && n->succ[k]==insert))n->succ[k]+=(int)count;
  }
  for(int i=0;i<f->nj;++i)if(f->jumpSuccessors[i]>=insert)f->jumpSuccessors[i]+=(int)count;
  for(unsigned i=0;i<count;++i) {
    f->nodes[insert+i]=initial[i];f->nodes[insert+i].succ[0]=insert+i+1;
  }
  wide_reset_allocation(f);
  return count;
}
