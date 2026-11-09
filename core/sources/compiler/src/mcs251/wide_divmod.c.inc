/* Reuse a quotient for a later remainder: a % b == a - (a / b) * b.
 * Operands are sampled MIR values, not source objects or SDK functions.
 * GPL-2.0-or-later. */
static bool
wide_divmod_operand_equal(wide_operand a,wide_operand b)
{
  return a.value==b.value && (a.value>=0 || a.literal==b.literal) &&
    a.width==b.width && a.sign==b.sign && !a.floating && !b.floating &&
    !a.codePointer && !b.codePointer;
}

static bool
wide_native_integer_call(wide_node *n,symbol *callee)
{
  return callee && callee->cdef && FUNC_SDCCCALL(callee->type)==4 &&
    n->op==CALL && n->callee==callee && !n->ordinaryCall && n->args==2 &&
    !n->stackArgs && n->width==4 && n->result>=0 && !n->floating &&
    n->left.width==4 && n->right.width==4 && !n->left.floating && !n->right.floating;
}

static unsigned
wide_reuse_integer_quotients(wide_function *f)
{
  /* A standalone remainder wrapper is smaller than introducing multiplication.
   * This rewrite trades those instructions for one fewer software division. */
  if(optimize.codeSize)return 0;
  unsigned changes=0;
  for(int at=0;at<f->nn;++at)for(unsigned sign=0;sign<2;++sign) {
    wide_node remainder=f->nodes[at];
    if(!wide_native_integer_call(&remainder,muldiv[2][2][sign]) ||
       !muldiv[0][2][0] || !muldiv[0][2][0]->cdef ||
       FUNC_SDCCCALL(muldiv[0][2][0]->type)!=4 ||
       f->nn>=f->nodeLimit || f->nv>=f->valueLimit)continue;
    int quotient=-1;
    wide_bits redefined={{0}};
    for(int from=at-1;from>=0;--from) {
      wide_node *n=&f->nodes[from];
      if(wide_sink_block_barrier(n) || n->succ[0]!=from+1 || n->succ[1]>=0 || n->jumpCount)break;
      if(wb_has(n->def,remainder.left.value) || wb_has(n->def,remainder.right.value))break;
      if(wide_native_integer_call(n,muldiv[1][2][sign]) &&
         wide_divmod_operand_equal(n->left,remainder.left) &&
         wide_divmod_operand_equal(n->right,remainder.right) &&
         !wb_has(redefined,n->result)) {quotient=n->result;break;}
      redefined=wb_or(redefined,n->def);
    }
    if(quotient<0)continue;
    int product=f->nv++;
    f->values[product]=(wide_value){.sym=f->values[remainder.result].sym,
      .width=4,.sign=remainder.sign,.reg=-1,.slot=-1,.pin=-1,.preference=-1};
    wide_node multiply=remainder;
    multiply.callee=muldiv[0][2][0];multiply.result=product;
    multiply.left=(wide_operand){.value=quotient,.width=4,.sign=remainder.sign};
    multiply.use=multiply.def=(wide_bits){{0}};
    wb_set(&multiply.use,quotient);wb_set(&multiply.use,multiply.right.value);wb_set(&multiply.def,product);
    remainder.op='-';remainder.callee=NULL;remainder.args=0;
    remainder.right=(wide_operand){.value=product,.width=4,.sign=remainder.sign};
    remainder.use=(wide_bits){{0}};
    wb_set(&remainder.use,remainder.left.value);wb_set(&remainder.use,product);
    memmove(&f->nodes[at+1],&f->nodes[at],(f->nn-at)*sizeof f->nodes[0]);
    ++f->nn;f->nodes[at]=multiply;f->nodes[at+1]=remainder;
    for(int i=0;i<f->nn;++i)for(int edge=0;edge<2;++edge)
      if(f->nodes[i].succ[edge]>at)++f->nodes[i].succ[edge];
    for(int i=0;i<f->nj;++i)if(f->jumpSuccessors[i]>at)++f->jumpSuccessors[i];
    f->nodes[at].succ[0]=at+1;
    dbuf_printf(f->diagnostics,"; wide quotient-reuse node=%d quotient=%d product=%d signed=%u\n",at,quotient,product,sign);
    ++at;++changes;break;
  }
  if(changes)wide_reset_allocation(f);
  return changes;
}
