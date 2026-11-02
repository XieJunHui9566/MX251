/* GPL-2.0-or-later. 有界 typed MIR 计划；不读取指令文本猜对象，不改原 IR。 */
#include "indexed_field_contract.c.inc"
static int
wif_root_ordinal(wide_function *f,int value,unsigned depth)
{
  if(value<0 || depth>12 || !f->values[value].pointer || f->values[value].codePointer || f->values[value].symbolSnapshot)return 0;
  wide_node *def=NULL;
  for(int k=0;k<f->nn;++k)if(wb_has(f->nodes[k].def,value)) {if(def)return 0;def=&f->nodes[k];}
  if(!def)return 0;
  if(def->op==RECEIVE)return def->origin->argreg;
  if((def->op=='=' || def->op==CAST) && def->right.value>=0 && f->values[def->right.value].pointer &&
     DCL_TYPE(f->values[value].sym->type)==DCL_TYPE(f->values[def->right.value].sym->type))
    return wif_root_ordinal(f,def->right.value,depth+1);
  return 0;
}

static unsigned
wide_native_indexed_fields(wide_function *f)
{
  if(!mcs251IndexedFieldEnabled || !mcs251IndexedFieldCapability || strcmp(mcs251IndexedFieldCapability,"source24-byte-v1"))return 0;
  unsigned changes=0;
  for(int k=0;k<f->nn;++k) {
    wide_node *n=&f->nodes[k];
    if(n->op!=WM_LOAD || n->addressOp!='+' || n->width!=1 || n->byteAccess || n->codeAccess || n->pointerAccess ||
       n->addressObject>=0 || n->left.value<0 || n->left.width!=4 || n->addressIndex.value>=0 ||
       n->addressIndex.floating || n->addressIndex.codePointer || !n->addressIndex.literal || n->addressIndex.literal>32767)continue;
    unsigned ordinal=wif_root_ordinal(f,n->left.value,0);sym_link *aggregate=NULL;
    const wif_contract *contract=ordinal?wif_parameter_contract(ordinal,&aggregate):NULL;
    if(!contract)continue;
    unsigned offset=n->addressIndex.literal;symbol *field=NULL;
    for(symbol *s=SPEC_STRUCT(aggregate)->fields;s;s=s->next)if(s->offset==offset && getSize(s->type)==1)field=s;
    if(!field || offset>=contract->extent || isVolatile(field->type) || isAtomic(field->type) || !IS_INTEGRAL(field->type))continue;
    /* 首版只提议紧接 byte 比较/逻辑的 load。此结构过滤避免一个明显
     * 活 flags 的 site 阻断其他 site；最终机器序列仍必须独立重证。 */
    int next=k+1;while(next<f->nn && f->nodes[next].op==WM_NOP)++next;
    if(next==f->nn)continue;
    wide_node *after=&f->nodes[next];
    bool compare=after->width==1 && (after->op==IFX || after->op=='<' || after->op=='>' ||
      after->op==LE_OP || after->op==GE_OP || after->op==EQ_OP || after->op==NE_OP);
    bool logic=after->width==1 && (after->op=='^' || after->op=='|' || after->op==BITWISEAND);
    if(!compare && !((offset==1 || offset==2 || offset==4) && logic))continue;
    /* 根只可经指针 copy/CAST 来自已绑定 RECEIVE，不能跨整数或地址算术。
     * 整对象的实际来源及目标普通栈区由两阶段闭包输入/最终 binder 再确认。 */
    n->nativeIndexed=true;n->indexedExtent=contract->extent;n->indexedOrdinal=ordinal;++changes;
  }
  f->nativeIndexedTrial=changes!=0;return changes;
}
