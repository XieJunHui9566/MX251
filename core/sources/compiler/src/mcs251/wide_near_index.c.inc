/* SPDX-License-Identifier: GPL-2.0-or-later
 * Dynamic DATA addresses use a byte value, just like the public near-pointer
 * representation. The origin is a known DATA object or an earlier internal
 * near value. The index is sampled at address formation, not at dereference. */
static int
wide_near_index_value(wide_function *f,operand *op)
{
  if(!op || !IS_ITEMP(op) || !wide_near_type(operandType(op)))return -1;
  for(int v=0;v<f->nv;++v)if(f->values[v].nearData && f->values[v].sym==OP_SYMBOL(op))return v;
  return -1;
}

static bool
wide_near_index_result(wide_function *f,wide_node *n,operand *op)
{
  if(!op || !IS_ITEMP(op) || IS_OP_GLOBAL(op) || OP_SYMBOL(op)->addrtaken ||
     !wide_near_type(operandType(op)) || isAtomic(operandType(op)))return false;
  int value=wide_near_index_value(f,op);
  if(value<0) {
    if(!wide_capacity_room(f,1,0,__LINE__))return false;
    value=f->nv++;
    f->values[value]=(wide_value){.sym=OP_SYMBOL(op),.width=1,.nearData=true,
      .reg=-1,.slot=-1,.pin=-1,.preference=-1};
    /* Earlier constant definitions of this same C temporary must participate
     * in branch merges and loop updates once its address becomes a value. */
    for(int i=0;i<f->nn;++i) {
      wide_node *d=&f->nodes[i];
      if(d->op==WM_NEAR_ADDRESS && d->result<0 && d->origin &&
         IC_RESULT(d->origin) && IS_SYMOP(IC_RESULT(d->origin)) &&
         OP_SYMBOL(IC_RESULT(d->origin))==OP_SYMBOL(op)) {
        d->result=value;d->width=1;wb_set(&d->def,value);
      }
    }
  }
  n->result=value;n->width=1;wb_set(&n->def,value);return true;
}

static int
wide_near_index_reference(wide_function *f,operand *op)
{
  int value=wide_near_index_value(f,op);
  if(value>=0 || !op || !IS_ITEMP(op) || !wide_near_type(operandType(op)))return value;
  /* Constant aggregate member views already have their own importer. Only
   * scalar DATA addresses need materialization for indexed access here. */
  sym_link *element=operandType(op)->next;
  if(!wide_function_pointer(element) && (!IS_SPEC(element) || !IS_INTEGRAL(element) || IS_BITVAR(element)))return -1;
  for(int i=0;i<f->nn;++i) {
    wide_node *d=&f->nodes[i];
    if(d->op==WM_NEAR_ADDRESS && d->result<0 && d->origin &&
       IC_RESULT(d->origin) && IS_SYMOP(IC_RESULT(d->origin)) &&
       OP_SYMBOL(IC_RESULT(d->origin))==OP_SYMBOL(op))
      return wide_near_index_result(f,d,op)?d->result:-1;
  }
  return -1;
}

static bool
wide_near_indexed(wide_function *f,wide_node *n,iCode *ic,iCode *begin)
{
  bool store=POINTER_SET(ic),load=ic->op==GET_VALUE_AT_ADDRESS;
  if(store || load) {
    operand *address=store?IC_RESULT(ic):IC_LEFT(ic),*value=store?IC_RIGHT(ic):IC_RESULT(ic);
    if(wide_near_index_reference(f,address)<0 || !value)return false;
    sym_link *pt=operandType(address),*t=operandType(value);
    bool pointer=wide_function_pointer(pt->next) && (wide_function_pointer(t) || (store && IS_FUNC(t)));
    if(isAtomic(pt->next) || isAtomic(t))return false;
    if(!pointer && (!IS_SPEC(pt->next) || !IS_INTEGRAL(pt->next) || IS_BITVAR(pt->next) ||
       !IS_SPEC(t) || !IS_INTEGRAL(t) || IS_BITVAR(t) ||
       isAtomic(pt->next) || isAtomic(t) || getSize(t)!=getSize(pt->next) ||
       (getSize(t)!=1 && getSize(t)!=2 && getSize(t)!=4)))return false;
    if(load && IC_RIGHT(ic) && (!IS_OP_LITERAL(IC_RIGHT(ic)) || operandLitValue(IC_RIGHT(ic))!=0))return false;
    if(!wide_use(f,n,address,&n->left))return false;
    if(store) {if(!wide_use(f,n,value,&n->right))return false;}
    else if(!wide_result(f,n,value))return false;
    n->op=store?WM_STORE:WM_LOAD;n->width=pointer?4:getSize(t);n->pointerAccess=pointer;
    n->byteAccess=isVolatile(pt->next) || isOperandVolatile(value,TRUE);
    f->body=true;
    dbuf_printf(f->diagnostics,"; wide indexed DATA %s icode=%d physical=%d address=byte volatile=%u\n",store?"store":"load",ic->key,n->width,n->byteAccess);
    return true;
  }
  if((ic->op=='+' || ic->op=='-') && IC_RESULT(ic) &&
     wide_near_type(operandType(IC_RESULT(ic))) && IC_RIGHT(ic) &&
     IS_INTEGRAL(operandType(IC_RIGHT(ic))) && !IS_BITVAR(operandType(IC_RIGHT(ic)))) {
    symbol *object=NULL;int offset=0;
    bool constant=wide_near_location(f,IC_LEFT(ic),ic==begin?NULL:ic->prev,begin,&object,&offset,0);
    if(!constant && wide_near_index_reference(f,IC_LEFT(ic))<0)return false;
    if(!wide_near_index_result(f,n,IC_RESULT(ic)))return false;
    if(constant) {
      if(!wide_use(f,n,IC_RIGHT(ic),&n->addressIndex))return false;
      n->op=WM_NEAR_ADDRESS;n->addressSymbol=object;n->addressOffset=offset;n->addressOp=ic->op;
    } else if(!wide_use(f,n,IC_LEFT(ic),&n->left) || !wide_use(f,n,IC_RIGHT(ic),&n->right))return false;
    f->body=true;return true;
  }
  if((ic->op==CAST || ic->op=='=') && wide_near_index_reference(f,IC_RIGHT(ic))>=0) {
    sym_link *target=operandType(IC_RESULT(ic));
    if(wide_near_type(target)) {
      if(!wide_near_index_result(f,n,IC_RESULT(ic)))return false;
    } else if(ic->op==CAST && wide_pointer(target) && DCL_TYPE(target)==GPOINTER) {
      if(!wide_result(f,n,IC_RESULT(ic)))return false;
    } else return false;
    if(!wide_use(f,n,IC_RIGHT(ic),&n->right))return false;
    n->op=CAST;f->body=true;return true;
  }
  return false;
}
