/* GPL-2.0-or-later. 只读的完整自动对象实参来源；不能授予机器/闭包资格。 */
static const iCode *
audit_object_definition(ebbIndex *ebbi,operand *op,const iCode *use)
{
  if(!op || !IS_ITEMP(op) || OP_SYMBOL(op)->addrtaken || !ebbi || ebbi->count>2048)return NULL;
  const iCode *found=NULL;const eBBlock *defBlock=NULL,*useBlock=NULL;unsigned walked=0;
  for(int b=0;b<ebbi->count;++b) {
    const eBBlock *bb=ebbi->bbOrder[b];if(!bb)return NULL;
    for(const iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      if(++walked>8192)return NULL;
      if(ic==use)useBlock=bb;
      if(!POINTER_SET(ic) && IC_RESULT(ic) && IS_SYMOP(IC_RESULT(ic)) &&
         OP_SYMBOL(IC_RESULT(ic))==OP_SYMBOL(op)) {
        if(found)return NULL;found=ic;defBlock=bb;
      }
    }
  }
  if(!found || !useBlock || !defBlock)return NULL;
  if(useBlock==defBlock) {
    for(const iCode *ic=defBlock->sch;ic;ic=ic==defBlock->ech?NULL:ic->next) {
      if(ic==use)return NULL;if(ic==found)return found;
    }
    return NULL;
  }
  return bitVectBitValue(useBlock->domVect,defBlock->bbnum)?found:NULL;
}

static symbol *
audit_complete_auto_object(ebbIndex *ebbi,operand *op,const iCode *use,unsigned depth)
{
  if(depth>12 || !op)return NULL;
  sym_link *pt=operandType(op);
  if(!IS_PTR(pt) || getSize(pt)!=3 || (DCL_TYPE(pt)!=FPOINTER && DCL_TYPE(pt)!=GPOINTER) ||
     isAtomic(pt) || isVolatile(pt))return NULL;
  const iCode *def=audit_object_definition(ebbi,op,use);if(!def)return NULL;
  if(def->op==ADDRESS_OF && IS_TRUE_SYMOP(IC_LEFT(def)) &&
     IC_RIGHT(def) && IS_OP_LITERAL(IC_RIGHT(def)) && operandLitValueUll(IC_RIGHT(def))==0) {
    symbol *s=OP_SYMBOL(IC_LEFT(def));char layout[2048]="";
    if(!options.stackAuto || !s->onStack || !s->level || s->_isparm ||
       IS_STATIC(s->etype) || IS_EXTERN(s->etype) || SPEC_ABSA(s->etype) ||
       isAtomic(s->type) || isVolatile(s->type) ||
       !mcs251FlatAggregateToken(s->type,layout,sizeof layout))return NULL;
    return s;
  }
  if(def->op=='=' || def->op==CAST) {
    operand *input=IC_RIGHT(def);sym_link *t=input?operandType(input):NULL;
    /* 本后端的 far/generic 都是线性 24 位表示；这里只追踪同宽
     * copy/CAST，最终必须继续到完整栈对象。近指针、CODE、整数和
     * 任何地址算术均不通过；实际发射的表示保持仍由 final binder 核对。 */
    if(!t || !IS_PTR(t) || getSize(t)!=3 ||
       (DCL_TYPE(t)!=FPOINTER && DCL_TYPE(t)!=GPOINTER))return NULL;
    return audit_complete_auto_object(ebbi,input,def,depth+1);
  }
  return NULL;
}

static void
audit_field_objects(const iCode *call,symbol *target,ebbIndex *ebbi)
{
  fputs(",\"actual_field_objects\":[",audit_file);
  value *formal=target && IS_FUNC(target->type)?FUNC_ARGS(target->type):NULL;
  for(unsigned ordinal=1;ordinal<=2;++ordinal) {
    if(ordinal>1)fputc(',',audit_file);
    fprintf(audit_file,"{\"ordinal\":%u",ordinal);
    char expected[2048]="",actual[2048]="";const iCode *send=NULL;symbol *object=NULL;
    sym_link *pt=formal?formal->type:NULL;unsigned walked=0;
    bool valid=call->op==CALL && target && FUNC_SDCCCALL(target->type)==4 && formal &&
      IS_PTR(pt) && getSize(pt)==3 && (DCL_TYPE(pt)==FPOINTER || DCL_TYPE(pt)==GPOINTER) &&
      !isVolatile(pt) && !isAtomic(pt) && !isVolatile(pt->next) && !isAtomic(pt->next) &&
      IS_REGPARM(formal->etype) && SPEC_ARGREG(formal->etype)==ordinal &&
      mcs251FlatAggregateToken(pt->next,expected,sizeof expected);
    if(valid)for(const iCode *p=call->prev;p && ++walked<=1024;p=p->prev) {
      if(p->eBBlockNum!=call->eBBlockNum || p->op==CALL || p->op==PCALL ||
         p->op==INLINEASM || p->op==LABEL || p->op==GOTO || p->op==IFX ||
         p->op==FUNCTION || p->op==RETURN || p->op==JUMPTABLE)break;
      if(p->op==SEND && p->argreg==ordinal) {if(send){valid=false;break;}send=p;}
    }
    if(valid && send && walked<=1024) {
      object=audit_complete_auto_object(ebbi,IC_LEFT(send),send,0);
      valid=object && mcs251FlatAggregateToken(object->type,actual,sizeof actual) && !strcmp(actual,expected);
    } else valid=false;
    if(valid) {
      fputs(",\"status\":\"complete-auto-object\",\"object_symbol_id\":",audit_file);audit_symbol_id(object);
      fputs(",\"object_type\":",audit_file);audit_type(object->type,1);
      fprintf(audit_file,",\"size_bytes\":%u,\"offset\":0,\"on_stack\":true,\"send_icode_key\":%d,\"source\":",getSize(object->type),send->key);
      audit_source(object->fileDef,object->lineDef);fputc('}',audit_file);
    } else fputs(",\"status\":\"unknown\",\"reason\":\"no-unique-dominating-complete-automatic-object-root\"}",audit_file);
    if(formal)formal=formal->next;
  }
  fputc(']',audit_file);
}
