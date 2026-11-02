/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅审计构建启用：保存后端入口的可变输入，不修改输入以迁就比较。
 * 不遍历任意 AST/全局堆；覆盖范围由此处和独立测试记录明确列出。 */
typedef struct wa_record {
  const void *address;
  size_t size;
  unsigned char *bytes;
  const char *kind;
  struct wa_record *next;
} wa_record;
typedef struct { wa_record *records; unsigned count; } wa_snapshot;

static bool wa_record_bytes(wa_snapshot *s,const void *p,size_t n,const char *kind)
{
  if(!p || !n)return false;
  for(wa_record *r=s->records;r;r=r->next)if(r->address==p && r->size==n)return false;
  wa_record *r=Safe_alloc(sizeof *r);r->address=p;r->size=n;r->kind=kind;
  r->bytes=Safe_alloc(n);memcpy(r->bytes,p,n);r->next=s->records;s->records=r;++s->count;
  return true;
}
#define WA_OBJECT(s,p,kind) wa_record_bytes(s,p,sizeof *(p),kind)
static void wa_bits(wa_snapshot *s,bitVect *v)
{
  if(WA_OBJECT(s,v,"bit-vector"))wa_record_bytes(s,v->vect,(size_t)v->allocSize*sizeof *v->vect,"bit-data");
}
static void wa_set(wa_snapshot *s,set *v)
{
  for(;v;v=v->next)if(!WA_OBJECT(s,v,"set-node"))break;
}
static void wa_type(wa_snapshot*,sym_link*);
static void wa_symbol(wa_snapshot*,symbol*);
static void wa_value(wa_snapshot *s,value *v)
{
  if(!WA_OBJECT(s,v,"value"))return;
  wa_type(s,v->type);wa_type(s,v->etype);wa_symbol(s,v->sym);wa_value(s,v->next);
}
static void wa_type(wa_snapshot *s,sym_link *t)
{
  if(!WA_OBJECT(s,t,"type"))return;
  if(IS_FUNC(t))wa_value(s,FUNC_ARGS(t));
  wa_type(s,t->next);
}
static void wa_symbol(wa_snapshot *s,symbol *v)
{
  if(!WA_OBJECT(s,v,"symbol"))return;
  wa_type(s,v->type);wa_type(s,v->etype);
  wa_bits(s,v->defs);wa_bits(s,v->uses);wa_bits(s,v->regsUsed);
}
static void wa_operand(wa_snapshot *s,operand *o)
{
  if(!WA_OBJECT(s,o,"operand"))return;
  wa_bits(s,o->usesDefs);
  switch(o->type) {
    case SYMBOL:wa_symbol(s,o->svt.symOperand);break;
    case VALUE:wa_value(s,o->svt.valOperand);break;
    case TYPE:wa_type(s,o->svt.typeOperand);break;
  }
}
static wa_snapshot *wa_begin(ebbIndex *ebbi)
{
  /* 对照：集合允许携带已有迭代游标。先建立入口状态，再开始快照。 */
  const char *stage=getenv("MX251_WIDE_AUDIT_STAGE");
  if(stage && !strcmp(stage,"seed-jump-cursor"))
    for(int b=0;b<ebbi->count;++b)
      for(iCode *ic=ebbi->bbOrder[b]->sch;ic;ic=ic==ebbi->bbOrder[b]->ech?NULL:ic->next)
        if(ic->op==JUMPTABLE && IC_JTLABELS(ic))IC_JTLABELS(ic)->curr=IC_JTLABELS(ic);
  wa_snapshot *s=Safe_alloc(sizeof *s);s->records=NULL;s->count=0;
  WA_OBJECT(s,ebbi,"ebb-index");
  wa_record_bytes(s,ebbi->bbOrder,ebbi->count*sizeof *ebbi->bbOrder,"bb-order");
  wa_record_bytes(s,ebbi->dfOrder,ebbi->count*sizeof *ebbi->dfOrder,"df-order");
  WA_OBJECT(s,&options,"options");WA_OBJECT(s,&optimize,"optimize");
  WA_OBJECT(s,&iCodeKey,"icode-key");WA_OBJECT(s,&operandKey,"operand-key");
  WA_OBJECT(s,codeOutBuf,"output-buffer");
  wa_record_bytes(s,dbuf_get_buf(codeOutBuf),dbuf_get_length(codeOutBuf),"output-data");
  wa_symbol(s,currFunc);
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];WA_OBJECT(s,bb,"basic-block");wa_symbol(s,bb->entryLabel);
    wa_set(s,bb->succList);wa_set(s,bb->predList);wa_set(s,bb->inExprs);
    wa_set(s,bb->outExprs);wa_set(s,bb->killedExprs);wa_set(s,bb->addrOf);
    bitVect *bits[]={bb->succVect,bb->domVect,bb->inDefs,bb->outDefs,bb->defSet,bb->ldefs,
      bb->usesDefs,bb->ptrsSet,bb->inPtrsSet,bb->ndompset,bb->linds};
    for(unsigned k=0;k<sizeof bits/sizeof *bits;++k)wa_bits(s,bits[k]);
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      WA_OBJECT(s,ic,"icode");wa_operand(s,ic->left);wa_operand(s,ic->right);wa_operand(s,ic->result);
      wa_symbol(s,ic->label);wa_set(s,ic->movedFrom);
      wa_bits(s,ic->rlive);wa_bits(s,ic->uses);wa_bits(s,ic->rUsed);wa_bits(s,ic->rMask);wa_bits(s,ic->rSurv);
      if(ic->op==IFX) {wa_symbol(s,IC_TRUE(ic));wa_symbol(s,IC_FALSE(ic));}
      if(ic->op==JUMPTABLE) {
        wa_set(s,IC_JTLABELS(ic));
        for(set *v=IC_JTLABELS(ic);v;v=v->next)wa_symbol(s,v->item);
      }
    }
  }
  return s;
}
static bool wa_end(wa_snapshot *s,mcs251_wide_result result)
{
  unsigned mismatches=0;
  for(wa_record *r=s->records,*next;r;r=next) {
    next=r->next;
    if(memcmp(r->bytes,r->address,r->size)) {
      ++mismatches;fprintf(stderr,"MX251-AUDIT changed=%s function=%s\n",r->kind,currFunc?currFunc->name:"initializer");
    }
    Safe_free(r->bytes);Safe_free(r);
  }
  fprintf(stderr,"MX251-AUDIT result=%d records=%u mismatches=%u function=%s\n",result,s->count,mismatches,currFunc?currFunc->name:"initializer");
  Safe_free(s);return !mismatches;
}
static bool wa_fail(const char *stage)
{
  const char *requested=getenv("MX251_WIDE_AUDIT_STAGE");
  if(!requested || strcmp(requested,stage))return false;
  const char *fn=getenv("MX251_WIDE_AUDIT_FUNCTION");
  return !fn || (currFunc && !strcmp(fn,currFunc->name));
}
#define WA_FAIL(stage) wa_fail(stage)
