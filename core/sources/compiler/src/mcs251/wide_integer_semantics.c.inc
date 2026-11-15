/* SPDX-License-Identifier: GPL-2.0-or-later
 * Recover full products of extended word snapshots before allocation.
 * The proof describes every definition's bits, not the formal signedness of
 * __mullong (also used for unsigned C multiplication). Memory reads stay put.
 */
enum { WNARROW_UNKNOWN, WNARROW_ZERO, WNARROW_SIGN, WNARROW_VISITING };

static unsigned wide_word_origin(wide_function *,wide_operand,unsigned char *);

static unsigned
wide_word_definition(wide_function *f,wide_node *n,unsigned char *known)
{
  if(n->floating || n->width!=4)return WNARROW_UNKNOWN;
  if(n->op=='=' || n->op==CAST)return wide_word_origin(f,n->right,known);
  if(n->op==WM_PARAM_LOAD && n->left.width<=2)
    return n->left.sign?WNARROW_SIGN:WNARROW_ZERO;
  return WNARROW_UNKNOWN;
}

static unsigned
wide_word_origin(wide_function *f,wide_operand o,unsigned char *known)
{
  if(o.floating || o.codePointer || (o.width!=1 && o.width!=2 && o.width!=4))return WNARROW_UNKNOWN;
  if(o.value<0) {
    uint32_t bits=o.literal;
    if(o.sign && o.width<4 && (bits&(UINT32_C(1)<<(8*o.width-1))))bits|=UINT32_MAX<<(8*o.width);
    if(bits<=65535)return WNARROW_ZERO;
    return bits>=UINT32_C(0xffff8000)?WNARROW_SIGN:WNARROW_UNKNOWN;
  }
  wide_value *v=&f->values[o.value];
  if(v->pointer || v->floating || v->codePointer)return WNARROW_UNKNOWN;
  if(o.width<=2)return o.sign?WNARROW_SIGN:WNARROW_ZERO;
  if(known[o.value])return known[o.value]==WNARROW_VISITING?WNARROW_UNKNOWN:known[o.value];
  known[o.value]=WNARROW_VISITING;
  unsigned result=WNARROW_UNKNOWN;bool defined=false;
  for(int i=0;i<f->nn;++i)if(wb_has(f->nodes[i].def,o.value)) {
    wide_node *n=&f->nodes[i];
    unsigned next=n->result==o.value?wide_word_definition(f,n,known):WNARROW_UNKNOWN;
    if(!next || (defined && next!=result)) {known[o.value]=0;return WNARROW_UNKNOWN;}
    result=next;defined=true;
  }
  known[o.value]=result;return result;
}

static void
wide_reduce_word_products(wide_function *f)
{
  if(!wide_arithmetic_semantics_enabled())return;
  unsigned char known[WV_MAX]={0};unsigned products=0,homes=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];unsigned op,width,sign;wide_operand rhs;
    if(n->op!=CALL || !wide_integer_runtime_contract(n->callee,&op,&width,&sign) ||
       op || width!=4 || !wide_runtime_binary(n,width,&rhs))continue;
    unsigned a=wide_word_origin(f,n->left,known),b=wide_word_origin(f,rhs,known);
    if(!a || !b)continue;
    n->op=WM_MUL16;n->right=rhs;
    n->multiplySignedMask=(a==WNARROW_SIGN?1:0)|(b==WNARROW_SIGN?2:0);
    n->callee=NULL;n->ordinaryCall=false;n->args=n->stackArgs=n->stackBytes=0;
    n->use=(wide_bits){{0}};wb_set(&n->use,n->left.value);wb_set(&n->use,n->right.value);
    dbuf_printf(f->diagnostics,"; wide word-origin product icode=%d signed=%u/%u abi-independent=1\n",
      n->origin->key,a==WNARROW_SIGN,b==WNARROW_SIGN);
    ++products;
  }
  /* An extended temporary used only for these products needs just its low
   * word. Keep its original definition point and snapshot lifetime; changing
   * the home does not reload the source or move a volatile transaction. */
  for(int v=0;v<f->nv && products;++v) {
    wide_value *value=&f->values[v];
    if(value->width!=4 || !known[v] || known[v]==WNARROW_VISITING || value->pin>=0 ||
       value->symbolSnapshot || value->pointer || value->floating || value->codePointer)continue;
    bool onlyProducts=true,used=false,defined=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(wb_has(n->def,v)) {
        defined=true;
        if(n->result!=v || (n->op!=CAST && n->op!='=') || n->width!=4)onlyProducts=false;
      }
      if(wb_has(n->use,v)) {
        used=true;
        if(n->op!=WM_MUL16 || (n->left.value!=v && n->right.value!=v))onlyProducts=false;
      }
    }
    if(!onlyProducts || !used || !defined)continue;
    value->width=2;value->sign=known[v]==WNARROW_SIGN;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(wb_has(n->def,v)) {n->width=2;n->sign=value->sign;}
      if(n->left.value==v) {n->left.width=2;n->left.sign=value->sign;}
      if(n->right.value==v) {n->right.width=2;n->right.sign=value->sign;}
    }
    ++homes;
  }
  if(products) {
    wide_reset_allocation(f);
    dbuf_printf(f->diagnostics,"; wide word-origin summary products=%u narrow-homes=%u\n",products,homes);
  }
}
