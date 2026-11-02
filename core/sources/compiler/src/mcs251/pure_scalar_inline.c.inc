/* SPDX-License-Identifier: GPL-2.0-or-later
 * 有界纯标量前端展开：只复用上游参数临时值、类型装饰和多 RETURN 处理。
 * 节点/深度/展开次数只限制资源，不冒充最终指令成本或运行周期。 */
extern int mcs251PureStaticScalarInlineEnabled;
#define M251_PSI_NODES 128
#define M251_PSI_STMTS 64
#define M251_PSI_VARS 24
#define M251_PSI_FUNCS 128

typedef struct {
  int kind;
  symbol *label, *yes, *no;
} m251_psi_stmt;
typedef struct {
  symbol *vars[M251_PSI_VARS];
  unsigned varsCount, nodes, count, returns;
  bool isVoid;
  const char *why;
  m251_psi_stmt statements[M251_PSI_STMTS];
} m251_psi_check;
typedef struct {
  ast *tree;
  unsigned nodes, args;
} m251_psi_function;
static m251_psi_function m251PsiFunctions[M251_PSI_FUNCS];
static unsigned m251PsiCount, m251PsiCallerNodes, m251PsiCallerExpansions;

static bool m251PsiReject(m251_psi_check *c,const char *why)
{ if(!c->why)c->why=why;return FALSE; }

static bool m251PsiScalar(sym_link *t)
{
  if(!t || !IS_SPEC(t) || !IS_INTEGRAL(t) || IS_BITVAR(t) ||
     !getSize(t) || getSize(t)>4 || IS_VOLATILE(t) || isAtomic(t) ||
     SPEC_ADDRSPACE(t) || SPEC_ABSA(t))return FALSE;
  /* 显式寻址存储不是普通自动标量；literal 仅在常量节点接受。 */
  return SPEC_SCLS(t)==S_FIXED || SPEC_SCLS(t)==S_AUTO ||
         SPEC_SCLS(t)==S_REGISTER || SPEC_SCLS(t)==S_LITERAL;
}

static symbol *m251PsiLookup(m251_psi_check *c,symbol *s)
{
  if(!s || s->implicit || s->islbl)return NULL;
  if(!c) {
    symbol *bound=findSymWithLevel(SymbolTab,s);
    return bound && bound->level && !IS_STATIC(bound->etype) && !IS_EXTERN(bound->etype) &&
           m251PsiScalar(bound->type) ? bound:NULL;
  }
  /* 当前 BLOCK 声明栈决定词法作用域；不把同名全局当作本函数参数。 */
  for(unsigned i=c->varsCount;i;--i) {
    symbol *d=c->vars[i-1];
    if(!strcmp(d->name,s->name) && d->level<=s->level &&
       (d->_isparm || d->seqPoint<=s->seqPoint))return d;
  }
  return NULL;
}

static bool m251PsiExpr(m251_psi_check *c,ast *t,unsigned depth,bool assignment)
{
  if(!t || depth>32 || t->isError || t->isStmtExpr)return FALSE;
  if(c && ++c->nodes>M251_PSI_NODES)return m251PsiReject(c,"expression-budget");
  if(IS_AST_VALUE(t)) {
    if(t->opval.val->sym)return m251PsiLookup(c,t->opval.val->sym)!=NULL;
    return IS_LITERAL(t->opval.val->etype) && m251PsiScalar(t->opval.val->type);
  }
  if(!IS_AST_OP(t))return FALSE;
  switch(t->opval.op) {
  case CAST:
    return t->left && IS_AST_LINK(t->left) && m251PsiScalar(t->left->opval.lnk) &&
           m251PsiExpr(c,t->right,depth+1,FALSE);
  case '=':
    return assignment && t->left && IS_AST_VALUE(t->left) && t->left->opval.val->sym &&
           m251PsiLookup(c,t->left->opval.val->sym) && m251PsiExpr(c,t->right,depth+1,FALSE);
  case '!':case '~':
    return !t->right && m251PsiExpr(c,t->left,depth+1,FALSE);
  case '+':case '-':
    return m251PsiExpr(c,t->left,depth+1,FALSE) &&
           (!t->right || m251PsiExpr(c,t->right,depth+1,FALSE));
  case '&':case '|':case '^':case '<':case '>':case LE_OP:case GE_OP:
  case EQ_OP:case NE_OP:case AND_OP:case OR_OP:case ':':
    return t->left && t->right && m251PsiExpr(c,t->left,depth+1,FALSE) &&
           m251PsiExpr(c,t->right,depth+1,FALSE);
  case '?':
    return t->right && IS_AST_OP(t->right) && t->right->opval.op==':' &&
           m251PsiExpr(c,t->left,depth+1,FALSE) && m251PsiExpr(c,t->right,depth+1,FALSE);
  case LEFT_OP:case RIGHT_OP:
    /* 变量移位及隐式库调用成本未知，第一版只允许非负小常量移位。 */
    return t->right && IS_AST_VALUE(t->right) && !t->right->opval.val->sym &&
           IS_LITERAL(t->right->opval.val->etype) && floatFromVal(t->right->opval.val)>=0 &&
           floatFromVal(t->right->opval.val)<32 && m251PsiExpr(c,t->left,depth+1,FALSE) &&
           m251PsiExpr(c,t->right,depth+1,FALSE);
  default:return FALSE;
  }
}

static bool m251PsiAdd(m251_psi_check *c,int kind,symbol *label,symbol *yes,symbol *no)
{
  if(c->count>=M251_PSI_STMTS)return m251PsiReject(c,"statement-budget");
  c->statements[c->count++]=(m251_psi_stmt){kind,label,yes,no};return TRUE;
}

static bool m251PsiStatement(m251_psi_check *c,ast *t,unsigned depth)
{
  if(!t)return TRUE;
  if(depth>32 || ++c->nodes>M251_PSI_NODES || t->isError || t->isStmtExpr)
    return m251PsiReject(c,"tree-budget-or-error");
  if(!IS_AST_OP(t))return m251PsiReject(c,"nonstatement");
  switch(t->opval.op) {
  case BLOCK: {
    unsigned saved=c->varsCount;
    if(t->left)return m251PsiReject(c,"block-left");
    for(symbol *s=t->values.sym;s;s=s->next) {
      if(c->varsCount>=M251_PSI_VARS || !s->level || IS_STATIC(s->etype) || IS_EXTERN(s->etype) || !m251PsiScalar(s->type) ||
         SPEC_SCLS(s->type)==S_LITERAL || s->isref || s->implicit)
        return m251PsiReject(c,"nonautomatic-local");
      c->vars[c->varsCount++]=s;
      if(s->ival && (s->ival->type!=INIT_NODE || s->ival->next ||
                    !m251PsiExpr(c,s->ival->init.node,depth+1,FALSE)))
        return m251PsiReject(c,"local-initializer");
    }
    bool ok=m251PsiStatement(c,t->right,depth+1);c->varsCount=saved;return ok;
  }
  case NULLOP:
    return m251PsiStatement(c,t->left,depth+1) && m251PsiStatement(c,t->right,depth+1);
  case LABEL:
    if(!t->left || !IS_AST_VALUE(t->left) || !t->left->opval.val->sym ||
       !t->left->opval.val->sym->islbl)return m251PsiReject(c,"label-shape");
    return m251PsiAdd(c,LABEL,t->left->opval.val->sym,NULL,NULL) &&
           m251PsiStatement(c,t->right,depth+1);
  case GOTO:
    if(t->right || !t->left || !IS_AST_VALUE(t->left) || !t->left->opval.val->sym ||
       !t->left->opval.val->sym->islbl)return m251PsiReject(c,"goto-shape");
    return m251PsiAdd(c,GOTO,NULL,t->left->opval.val->sym,NULL);
  case IFX:
    if(t->right || !m251PsiExpr(c,t->left,depth+1,FALSE))return m251PsiReject(c,"condition-effects");
    return m251PsiAdd(c,IFX,NULL,t->trueLabel,t->falseLabel);
  case RETURN:
    if(t->left || (c->isVoid ? t->right!=NULL:!m251PsiExpr(c,t->right,depth+1,FALSE)))
      return m251PsiReject(c,"return-type-or-effects");
    ++c->returns;return m251PsiAdd(c,RETURN,NULL,NULL,NULL);
  default:
    return m251PsiExpr(c,t,depth+1,TRUE) ? m251PsiAdd(c,0,NULL,NULL,NULL):
           m251PsiReject(c,"unsupported-statement-or-effect");
  }
}

static int m251PsiLabel(m251_psi_check *c,symbol *s)
{
  int found=-1;
  if(!s || !s->islbl)return -1;
  for(unsigned i=0;i<c->count;++i)
    if(c->statements[i].kind==LABEL && !strcmp(c->statements[i].label->name,s->name)) {
      if(found>=0)return -1;found=(int)i;
    }
  return found;
}

static bool m251PsiFlow(m251_psi_check *c)
{
  bool reaches[M251_PSI_STMTS+1]={TRUE};
  if(!c->count || (!c->isVoid && !c->returns))return m251PsiReject(c,"missing-return");
  for(unsigned i=0;i<c->count;++i) {
    m251_psi_stmt *s=&c->statements[i];
    if(s->kind==LABEL && m251PsiLabel(c,s->label)!=(int)i)return m251PsiReject(c,"duplicate-label");
    if(s->kind==GOTO || s->kind==IFX) {
      symbol *edges[2]={s->yes,s->no};
      for(unsigned n=0;n<(s->kind==GOTO?1u:2u);++n) {
        int target=edges[n]?m251PsiLabel(c,edges[n]):(int)i+1;
        if(target<=(int)i || target>(int)c->count)return m251PsiReject(c,"nonforward-or-unclosed-edge");
        if(reaches[i])reaches[target]=TRUE;
      }
    } else if(s->kind!=RETURN && reaches[i])reaches[i+1]=TRUE;
  }
  return c->isVoid || !reaches[c->count] || m251PsiReject(c,"nonvoid-fallthrough");
}

static int m251PsiFind(ast *tree)
{
  if(tree)for(unsigned i=0;i<m251PsiCount;++i)if(m251PsiFunctions[i].tree==tree)return (int)i;
  return -1;
}

static bool m251PsiAttrs(sym_link *t)
{
  return IS_FUNC(t) && !FUNC_HASVARARGS(t) && !FUNC_ISISR(t) && !FUNC_ISCRITICAL(t) &&
    !FUNC_ISNAKED(t) && !FUNC_BANKED(t) && !FUNC_REGBANK(t) && !t->funcAttrs.rbank &&
    !FUNC_CALLEESAVES(t) && !FUNC_ISNORETURN(t) && !FUNC_NOPROTOTYPE(t) &&
    !t->funcAttrs.oldStyle && !FUNC_ISBUILTIN(t) && !FUNC_ISOVERLAY(t) &&
    !FUNC_ISJAVANATIVE(t) && !FUNC_ISSHADOWREGS(t) && !FUNC_ISWPARAM(t) &&
    !t->funcAttrs.smallc && !t->funcAttrs.raisonance && !t->funcAttrs.iar &&
    !t->funcAttrs.cosmic && !t->funcAttrs.dynamicc && !t->funcAttrs.z88dk_fastcall &&
    !t->funcAttrs.z88dk_callee && !t->funcAttrs.z88dk_shortcall && !t->funcAttrs.z88dk_has_params_offset &&
    (FUNC_SDCCCALL(t)==2 || FUNC_SDCCCALL(t)==4);
}

static int m251PsiSelect(symbol *name,ast *body)
{
  m251PsiCallerNodes=m251PsiCallerExpansions=0;
  if(!TARGET_IS_MCS251 || !mcs251PureStaticScalarInlineEnabled)return -1;
  int existing=m251PsiFind(name->funcTree);
  if(existing>=0)return existing;
  if(FUNC_ISINLINE(name->type) || !IS_STATIC(name->etype) || IS_EXTERN(name->etype))return -1;
  m251_psi_check check={0};unsigned argc=0;
  check.isVoid=IS_VOID(name->type->next);
  const char *reason=NULL;
  if(!m251PsiAttrs(name->type))reason="function-attributes-or-ABI";
  else if(!check.isVoid && !m251PsiScalar(name->type->next))reason="return-type";
  if(!reason)for(value *v=FUNC_ARGS(name->type);v;v=v->next) {
    if(++argc>6 || !v->sym || !v->sym->_isparm || !m251PsiScalar(v->type)) {reason="parameter-type-or-count";break;}
    check.vars[check.varsCount++]=v->sym;
  }
  if(!reason && (!m251PsiStatement(&check,body,0) || !m251PsiFlow(&check)))reason=check.why?check.why:"pure-body-rejected";
  if(!reason && m251PsiCount>=M251_PSI_FUNCS)reason="translation-unit-budget";
  if(reason) {
    fprintf(stderr,"MX251 pure-static-scalar-inline reject function=%s line=%d reason=%s\n",name->name,name->lineDef,reason);
    return -1;
  }
  unsigned slot=m251PsiCount++;
  m251PsiFunctions[slot]=(m251_psi_function){NULL,check.nodes,argc};
  FUNC_ISINLINE(name->type)=1;
  fprintf(stderr,"MX251 pure-static-scalar-inline eligible function=%s line=%d nodes=%u args=%u ABI=%d\n",
          name->name,name->lineDef,check.nodes,argc,FUNC_SDCCCALL(name->type));
  return (int)slot;
}

static void m251PsiBind(int slot,ast *body)
{ if(slot>=0)m251PsiFunctions[slot].tree=body; }

static bool m251PsiCall(symbol *callee,ast *call)
{
  int slot=m251PsiFind(callee->funcTree);
  if(slot<0)return TRUE; /* 不改变用户显式 inline 的既有行为。 */
  m251_psi_function *f=&m251PsiFunctions[slot];
  const char *reason=NULL;
  if(!currFunc || FUNC_ISISR(currFunc->type) || FUNC_ISCRITICAL(currFunc->type) || FUNC_ISNAKED(currFunc->type))reason="caller-boundary";
  else if(m251PsiCallerExpansions>=8 || m251PsiCallerNodes+f->nodes>512)reason="caller-expansion-budget";
  if(!reason)for(unsigned i=0;i<f->args;++i)
    if(!m251PsiExpr(NULL,inlineFindParm(call->right,(int)i),0,FALSE)) {reason="actual-argument-effects-or-unknown-type";break;}
  if(!reason && inlineFindParm(call->right,(int)f->args))reason="argument-count";
  if(reason) {
    fprintf(stderr,"MX251 pure-static-scalar-inline retain-call caller=%s callee=%s line=%d reason=%s\n",
            currFunc?currFunc->name:"?",callee->name,call->lineno,reason);return FALSE;
  }
  m251PsiCallerNodes+=f->nodes;++m251PsiCallerExpansions;
  fprintf(stderr,"MX251 pure-static-scalar-inline expand caller=%s callee=%s line=%d ordinal=%u nodes=%u ABI=%d\n",
          currFunc->name,callee->name,call->lineno,m251PsiCallerExpansions,f->nodes,FUNC_SDCCCALL(callee->type));
  return TRUE;
}
