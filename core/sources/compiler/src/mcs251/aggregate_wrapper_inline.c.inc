/* SPDX-License-Identifier: GPL-2.0-or-later
 * 有界聚合 wrapper 只开放传统内联；不传播字段、不推断 callee noescape。
 * 原 call 及自动对象交由原前端生成，未知效果仍是完整调用屏障。 */
extern int mcs251AggregateWrapperInlineEnabled;
#define M251_AWI_FUNCS 128
typedef struct {
  m251_psi_check scalar;
  symbol *object, *fields[8], *callee;
  unsigned fieldCount, initialized, statements, returned;
} m251_awi_check;
typedef struct { ast *tree; unsigned nodes,args,bytes; symbol *callee; } m251_awi_function;
static m251_awi_function m251AwiFunctions[M251_AWI_FUNCS];
static unsigned m251AwiCount,m251AwiCallerNodes,m251AwiCallerExpansions;

static bool m251AwiNodes(ast *t,unsigned depth,unsigned *count)
{
  if(!t)return TRUE;
  if(depth>32 || ++*count>128 || t->isError || t->isStmtExpr)return FALSE;
  return m251AwiNodes(t->left,depth+1,count) && m251AwiNodes(t->right,depth+1,count);
}

static bool m251AwiObject(m251_awi_check *c,symbol *s)
{
  if(!s || !s->level || s->implicit || s->ival || s->next || !IS_STRUCT(s->type) ||
     IS_STATIC(s->etype) || IS_EXTERN(s->etype) || IS_VOLATILE(s->type) || isAtomic(s->type) ||
     SPEC_ABSA(s->etype) || SPEC_ADDRSPACE(s->etype) ||
     (SPEC_SCLS(s->etype)!=S_FIXED && SPEC_SCLS(s->etype)!=S_AUTO))return FALSE;
  structdef *d=SPEC_STRUCT(s->etype);
  if(!d || d->type!=STRUCT || d->b_flexArrayMember || !d->size || d->size>16)return FALSE;
  for(symbol *f=d->fields;f;f=f->next) {
    if(c->fieldCount>=8 || !m251PsiScalar(f->type) || IS_BITFIELD(f->type))return FALSE;
    c->fields[c->fieldCount++]=f;
  }
  if(!c->fieldCount)return FALSE;
  c->object=s;return TRUE;
}

static bool m251AwiObjectRef(m251_awi_check *c,ast *t)
{
  if(!t || !IS_AST_VALUE(t) || !t->opval.val->sym)return FALSE;
  symbol *s=t->opval.val->sym;
  return !s->implicit && !strcmp(s->name,c->object->name) &&
         s->level==c->object->level && s->block==c->object->block;
}

static bool m251AwiAddress(m251_awi_check *c,ast *t)
{ return t && IS_AST_OP(t) && t->opval.op=='&' && !t->right && m251AwiObjectRef(c,t->left); }

static int m251AwiField(m251_awi_check *c,ast *t)
{
  /* .field 在 parser 中为 PTR_OP(&object, implicit-field)。不接受 ->。 */
  if(!t || !IS_AST_OP(t) || t->opval.op!=PTR_OP || !m251AwiAddress(c,t->left) ||
     !t->right || !IS_AST_VALUE(t->right) || !t->right->opval.val->sym)return -1;
  symbol *s=t->right->opval.val->sym;
  if(!s->implicit)return -1;
  for(unsigned i=0;i<c->fieldCount;++i)if(!strcmp(s->name,c->fields[i]->name))return (int)i;
  return -1;
}

static bool m251AwiInnerCall(m251_awi_check *c,ast *t)
{
  if(!t || !IS_AST_OP(t) || t->opval.op!=CALL || !t->left || !IS_AST_VALUE(t->left) ||
     !t->left->opval.val->sym)return FALSE;
  symbol *raw=t->left->opval.val->sym;
  symbol *callee=findSymWithLevel(SymbolTab,raw);
  if(!callee || callee->level || raw->implicit || !m251PsiAttrs(callee->type) ||
     FUNC_ISINLINE(callee->type) || !m251PsiScalar(callee->type->next))return FALSE;
  unsigned index=0,addresses=0;
  for(value *v=FUNC_ARGS(callee->type);v;v=v->next,++index) {
    if(index>=6)return FALSE;
    ast *actual=inlineFindParm(t->right,(int)index);
    if(m251AwiAddress(c,actual)) {
      if(++addresses!=1 || !IS_PTR(v->type) || !IS_STRUCT(v->type->next) ||
         IS_VOLATILE(v->type->next) || isAtomic(v->type->next) ||
         SPEC_STRUCT(v->type->next)!=SPEC_STRUCT(c->object->etype))return FALSE;
    } else if(!m251PsiScalar(v->type) || !m251PsiExpr(&c->scalar,actual,0,FALSE))return FALSE;
  }
  if(addresses!=1 || inlineFindParm(t->right,(int)index))return FALSE;
  c->callee=callee;return TRUE;
}

static bool m251AwiReturn(m251_awi_check *c,ast *t)
{
  if(t && IS_AST_OP(t) && (t->opval.op==EQ_OP || t->opval.op==NE_OP)) {
    if(!t->right || !IS_AST_VALUE(t->right) || t->right->opval.val->sym ||
       !IS_LITERAL(t->right->opval.val->etype) || !m251PsiScalar(t->right->opval.val->type))return FALSE;
    t=t->left;
  }
  return m251AwiInnerCall(c,t);
}

static bool m251AwiStatements(m251_awi_check *c,ast *t,unsigned depth)
{
  if(!t)return TRUE;
  if(depth>32 || !IS_AST_OP(t) || ++c->statements>64)return FALSE;
  if(t->opval.op==NULLOP)
    return m251AwiStatements(c,t->left,depth+1) && m251AwiStatements(c,t->right,depth+1);
  if(c->returned)return FALSE;
  if(t->opval.op=='=') {
    int f=m251AwiField(c,t->left);
    if(f<0 || (c->initialized&(1u<<f)) || !m251PsiExpr(&c->scalar,t->right,0,FALSE))return FALSE;
    c->initialized|=1u<<f;return TRUE;
  }
  if(t->opval.op==RETURN && !t->left && c->initialized==((1u<<c->fieldCount)-1) && m251AwiReturn(c,t->right)) {
    ++c->returned;return TRUE;
  }
  return FALSE;
}

static int m251AwiFind(ast *tree)
{
  if(tree)for(unsigned i=0;i<m251AwiCount;++i)if(m251AwiFunctions[i].tree==tree)return (int)i;
  return -1;
}

static int m251AwiSelect(symbol *name,ast *body)
{
  m251AwiCallerNodes=m251AwiCallerExpansions=0;
  if(!TARGET_IS_MCS251 || !mcs251AggregateWrapperInlineEnabled)return -1;
  int existing=m251AwiFind(name->funcTree);
  if(existing>=0)return existing;
  if(FUNC_ISINLINE(name->type) || !IS_STATIC(name->etype) || IS_EXTERN(name->etype))return -1;
  m251_awi_check c={0};unsigned argc=0,nodes=0;const char *reason=NULL;
  /* 强制栈自动对象以支持同一调用者的重入，不借用 legacy overlay。 */
  if(!options.stackAuto || !m251PsiAttrs(name->type))reason="stack-auto-or-function-ABI";
  else if(!m251PsiScalar(name->type->next))reason="return-type";
  if(!reason)for(value *v=FUNC_ARGS(name->type);v;v=v->next) {
    if(++argc>6 || !v->sym || !v->sym->_isparm || !m251PsiScalar(v->type)) {reason="parameter-type-or-count";break;}
    c.scalar.vars[c.scalar.varsCount++]=v->sym;
  }
  if(!reason && (!argc || !m251AwiNodes(body,0,&nodes)))reason="tree-budget-or-parameter-count";
  if(!reason && (!body || !IS_AST_OP(body) || body->opval.op!=BLOCK || body->left ||
                !m251AwiObject(&c,body->values.sym)))reason="automatic-flat-object";
  if(!reason && (!m251AwiStatements(&c,body->right,0) || c.returned!=1))reason="field-initialization-or-single-call-shape";
  if(!reason && m251AwiCount>=M251_AWI_FUNCS)reason="translation-unit-budget";
  if(reason) {
    fprintf(stderr,"MX251 aggregate-wrapper-inline reject function=%s line=%d reason=%s\n",name->name,name->lineDef,reason);return -1;
  }
  unsigned slot=m251AwiCount++;
  m251AwiFunctions[slot]=(m251_awi_function){NULL,nodes,argc,getSize(c.object->type),c.callee};
  FUNC_ISINLINE(name->type)=1;
  fprintf(stderr,"MX251 aggregate-wrapper-inline eligible function=%s line=%d nodes=%u args=%u objectBytes=%u ABI=%d\n",
          name->name,name->lineDef,nodes,argc,getSize(c.object->type),FUNC_SDCCCALL(name->type));
  return (int)slot;
}

static void m251AwiBind(int slot,ast *body)
{ if(slot>=0)m251AwiFunctions[slot].tree=body; }

static bool m251AwiCall(symbol *callee,ast *call)
{
  int slot=m251AwiFind(callee->funcTree);
  if(slot<0)return TRUE;
  m251_awi_function *f=&m251AwiFunctions[slot];const char *reason=NULL;
  if(!currFunc || !options.stackAuto || !m251PsiAttrs(currFunc->type))reason="caller-boundary";
  else if(m251AwiCallerExpansions>=4 || m251AwiCallerNodes+f->nodes>512)reason="caller-expansion-budget";
  /* fixupInline 会提升所有 leaf 的 level；避免原全局调用被 caller 局部捕获。
   * 保守拒绝表中任意同名局部（即使词法可见性尚待 resolveSymbols 计算）。 */
  if(!reason)for(unsigned i=0;i<HASHTAB_SIZE;++i)
    for(bucket *b=SymbolTab[i];b;b=b->next)
      if(b->level && !strcmp(b->name,f->callee->name)) {reason="callee-name-local-capture";break;}
  if(!reason)for(unsigned i=0;i<f->args;++i)
    if(!m251PsiExpr(NULL,inlineFindParm(call->right,(int)i),0,FALSE)) {reason="actual-argument-effects-or-unknown-type";break;}
  if(!reason && inlineFindParm(call->right,(int)f->args))reason="argument-count";
  if(reason) {
    fprintf(stderr,"MX251 aggregate-wrapper-inline retain-call caller=%s callee=%s line=%d reason=%s\n",
            currFunc?currFunc->name:"?",callee->name,call->lineno,reason);return FALSE;
  }
  m251AwiCallerNodes+=f->nodes;++m251AwiCallerExpansions;
  fprintf(stderr,"MX251 aggregate-wrapper-inline expand caller=%s callee=%s line=%d ordinal=%u nodes=%u objectBytes=%u ABI=%d\n",
          currFunc->name,callee->name,call->lineno,m251AwiCallerExpansions,f->nodes,f->bytes,FUNC_SDCCCALL(callee->type));
  return TRUE;
}
