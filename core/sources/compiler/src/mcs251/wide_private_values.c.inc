/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An inlined helper can leave a local's addrtaken flag set even though all
 * pointer uses have become direct object accesses. Recover such scalars too. */
static unsigned
wide_private_stack_values(wide_function *f)
{
  bool removed[WO_MAX]={0};int map[WO_MAX];unsigned count=0;
  for(int object=0;object<f->no;++object) {
    symbol *s=f->objects[object].sym;sym_link *type=s->type;
    if(!s->onStack || !wide_abi_type(type,currFunc->type) ||
       isVolatile(type) || isAtomic(type) || f->nv==f->valueLimit)continue;
    int width=wide_width(type);bool floating=IS_FLOAT(type),pointer=wide_pointer(type);
    bool sign=!floating && !pointer && !SPEC_USIGN(getSpec(type));
    unsigned loads=0,stores=0;bool valid=true;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->addressObject!=object || n->op==WM_NOP)continue;
      if(n->addressOffset || n->addressOp || n->byteAccess || n->orderedReadCount ||
         n->width!=width || n->sign!=sign || n->floating!=floating ||
         n->pointerAccess!=pointer || n->codeAccess) {valid=false;break;}
      if(n->op==WM_LOAD || n->op==WM_OBJECT_LOAD)++loads;
      else if(n->op==WM_STORE || n->op==WM_OBJECT_STORE)++stores;
      else {valid=false;break;}
    }
    if(!valid || !loads || !stores)continue;
    int home=f->nv++;
    f->values[home]=(wide_value){.sym=s,.width=width,.sign=sign,.pointer=pointer,
      .floating=floating,.codePointer=wide_code_pointer(type),.symbolSnapshot=true,
      .reg=-1,.slot=-1,.pin=-1,.preference=-1};
    wide_operand input={.value=home,.width=width,.sign=sign,.floating=floating,
      .codePointer=wide_code_pointer(type)};
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];if(n->addressObject!=object)continue;
      if(n->op==WM_LOAD || n->op==WM_OBJECT_LOAD) {
        n->op=CAST;n->right=input;n->use=(wide_bits){{0}};wb_set(&n->use,home);
        f->values[n->result].symbolSnapshot=false;
      } else if(n->op==WM_STORE || n->op==WM_OBJECT_STORE) {
        n->op='=';n->result=home;n->def=(wide_bits){{0}};wb_set(&n->def,home);
      }
      n->left=(wide_operand){.value=-1};n->addressObject=-1;n->addressSymbol=NULL;
      n->addressOffset=0;n->pointerAccess=false;
    }
    removed[object]=true;++count;
    dbuf_printf(f->diagnostics,"; wide private-stack value object=%d physical=%d loads=%u stores=%u value=%d\n",
      object,f->objects[object].size,loads,stores,home);
  }
  if(!count)return 0;
  int objects=0,frame=0;
  for(int i=0;i<f->no;++i) {
    if(removed[i]) {map[i]=-1;continue;}
    map[i]=objects;wide_object object=f->objects[i];object.slot=frame;
    frame+=object.size;f->objects[objects++]=object;
  }
  for(int i=0;i<f->nn;++i)if(f->nodes[i].addressObject>=0)
    f->nodes[i].addressObject=map[f->nodes[i].addressObject];
  f->no=objects;f->frame=frame;f->promotedStackObjects=true;wide_reset_allocation(f);
  return count;
}
