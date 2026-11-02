/* GPL-2.0-or-later. 普通后端的单调用区域参数化共享。 */
#include "call_region.h"
typedef struct {
  lineNode *node; iCode *parameterCall;
  unsigned run; bool used;
} m251_call_line;
typedef struct {
  char key[M251_OUTLINE_MAX][M251_OUTLINE_TEXT];
  char body[M251_OUTLINE_MAX][M251_OUTLINE_TEXT];
  const char *callee; unsigned length,growth; bool internalArgument;
} m251_call_sequence;

static bool m251CallRegionScalar(sym_link *type)
{
  return type && IS_SPEC(type) && !IS_STRUCT(type) && !IS_VOID(type) &&
         getSize(type)>0 && getSize(type)<=4 && !isAtomic(type) && !isVolatile(type);
}
static bool m251CallRegionIR(lineNode *p)
{
  if (!p->ic || p->isInline) return FALSE;
  operand *ops[3]={IC_LEFT(p->ic),IC_RIGHT(p->ic),IC_RESULT(p->ic)};
  for (unsigned i=0;i<3;++i) if (ops[i]) {
    sym_link *t=operandType(ops[i]);
    if (isAtomic(t) || mcs251TransferObservable(ops[i]) ||
        (IS_PTR(t) && (isAtomic(t->next) || isVolatile(t->next)))) return FALSE;
  }
  return TRUE;
}
static bool m251CallRegionCallee(lineNode *p,const char **name,unsigned *bytes)
{
  if (!p->ic || p->ic->op!=CALL || !IS_SYMOP(IC_LEFT(p->ic))) return FALSE;
  sym_link *t=operandType(IC_LEFT(p->ic));
  if (!IS_FUNC(t) || FUNC_SDCCCALL(t)!=2 || !IFFUNC_ISREENT(t) ||
      IFFUNC_HASVARARGS(t) || IFFUNC_ISNAKED(t) || IFFUNC_CALLEESAVES(t) ||
      IFFUNC_ISISR(t) || IFFUNC_ISBANKEDCALL(t) || FUNC_REGBANK(t) ||
      SPEC_NORETURN(getSpec(t)) || !m251CallRegionScalar(t->next)) return FALSE;
  for (value *v=FUNC_ARGS(t);v;v=v->next) if (!m251CallRegionScalar(v->type)) return FALSE;
  if (p->ic->parmBytes<0 || p->ic->parmBytes>M251_CALL_REGION_STACK) return FALSE;
  *name=OP_SYMBOL(IC_LEFT(p->ic))->rname;*bytes=(unsigned)p->ic->parmBytes;
  char expected[M251_OUTLINE_TEXT],normalized[M251_OUTLINE_TEXT];unsigned n=0;
  for (const char *q=p->line;*q && *q!=';';++q) if (!isspace((unsigned char)*q)) {
    if (n+1>=sizeof normalized) return FALSE;normalized[n++]=*q;
  }
  normalized[n]=0;snprintf(expected,sizeof expected,"ecall%s",*name);
  return !strcmp(normalized,expected);
}
static bool m251CallRegionSequence(m251_call_line *lines,unsigned start,unsigned length,
                                   m251_call_sequence *seq,unsigned scratch)
{
  m251_call_stack stack={0};iCode *parameterCall=NULL;
  seq->callee=NULL;seq->growth=0;seq->length=length;seq->internalArgument=FALSE;
  for (unsigned i=0;i<length;++i) {
    m251_call_line *l=lines+start+i;lineNode *p=l->node;
    if (l->used || l->run!=lines[start].run) return FALSE;
    const char *name=NULL;unsigned bytes=0;
    if (lineIsInst(p,"ecall")) {
      if (!m251CallRegionCallee(p,&name,&bytes) ||
          (parameterCall && parameterCall!=p->ic) || !m251CallStackInvoke(&stack,bytes)) return FALSE;
      seq->callee=name;
      seq->internalArgument=mcs251InternalArgument(OP_SYMBOL(IC_LEFT(p->ic)));
      snprintf(seq->key[i],M251_OUTLINE_TEXT,"call abi=2 stack=%u return=%u internal-ret=%u internal-arg=%u",bytes,getSize(operandType(IC_LEFT(p->ic))->next),
        mcs251InternalReturn(OP_SYMBOL(IC_LEFT(p->ic))),mcs251InternalArgument(OP_SYMBOL(IC_LEFT(p->ic))));
      snprintf(seq->body[i],M251_OUTLINE_TEXT,"ecall @dr%u",scratch);
    } else {
      if (!m251CallRegionIR(p)) return FALSE;
      if (l->parameterCall) {
        if (parameterCall && parameterCall!=l->parameterCall) return FALSE;
        parameterCall=l->parameterCall;
      }
      if (!m251CallRegionLine(p->line,&stack,l->parameterCall!=NULL,
                             seq->key[i],seq->body[i],&seq->growth)) return FALSE;
    }
  }
  return stack.calls==1 && stack.depth==0;
}
static bool m251CallRegionEqual(const m251_call_sequence *a,const m251_call_sequence *b)
{
  if (a->length!=b->length || a->growth!=b->growth) return FALSE;
  for (unsigned i=0;i<a->length;++i)
    if (strcmp(a->key[i],b->key[i]) || strcmp(a->body[i],b->body[i])) return FALSE;
  return TRUE;
}
static void mcs251OutlineCallRegions(lineNode *head,struct dbuf_s *helpers)
{
  const char *off=getenv("MX251_DISABLE_CALL_REGION");
  bool trace=getenv("MX251_CALL_REGION_TRACE")!=NULL;
  if (trace && currFunc) fprintf(stderr,"call-region function=%s abi=%d isr=%d callee-saves=%d bank=%d\n",currFunc->name,FUNC_SDCCCALL(currFunc->type),IFFUNC_ISISR(currFunc->type),IFFUNC_CALLEESAVES(currFunc->type),FUNC_REGBANK(currFunc->type));
  if ((off && !strcmp(off,"1")) || !currFunc || options.useXstack ||
      FUNC_SDCCCALL(currFunc->type)!=2 || IFFUNC_ISNAKED(currFunc->type) ||
      IFFUNC_CALLEESAVES(currFunc->type) || IFFUNC_ISISR(currFunc->type) ||
      FUNC_REGBANK(currFunc->type)) return;
  unsigned capacity=0,count=0,run=1,scratch=28,usedScratch=0;lineNode *last=NULL;
  /* 普通分配器无 DR24/DR28 字节视图，且整个函数不得已使用选中 scratch。
   * 这比假定某一调用点死亡更保守；未知文本也不能越过此检查。 */
  for (lineNode *p=head;p;p=p->next) {
    if (p->isInline || (p->ic && (p->ic->op==CRITICAL || p->ic->op==ENDCRITICAL))) return;
    if (!p->isComment && !p->isDebug) {
      if (strstr(p->line,"dr28") || strstr(p->line,"wr28") || strstr(p->line,"wr30")) usedScratch|=1;
      if (strstr(p->line,"dr24") || strstr(p->line,"wr24") || strstr(p->line,"wr26")) usedScratch|=2;
    }
    if (!p->isComment && !p->isDebug && !p->isLabel) last=p;
    ++capacity;
  }
  if (usedScratch==3) return;
  if (usedScratch&1) scratch=24;
  if (!last || (!lineIsInst(last,"eret") && !lineIsInst(last,"ret") &&
      !lineIsInst(last,"ejmp") && !lineIsInst(last,"ljmp") && !lineIsInst(last,"sjmp"))) return;
  m251_call_line *lines=Safe_alloc(capacity*sizeof(*lines));iCode *parameter=NULL;
  for (lineNode *p=head;p;p=p->next) {
    if (p->isComment) {
      if (strstr(p->line,"MX251 argument region begin") && p->ic && p->ic->op==IPUSH && p->ic->parmPush) {
        parameter=NULL;
        for (iCode *ic=p->ic;ic;ic=ic->next) if (ic->op==CALL || ic->op==PCALL) {parameter=ic;break;}
      } else if (strstr(p->line,"MX251 argument region end")) parameter=NULL;
      continue;
    }
    if (p->isDebug) continue;
    if (p->isLabel || !p->ic) {++run;parameter=NULL;continue;}
    lines[count].node=p;lines[count].parameterCall=parameter;lines[count].run=run;lines[count].used=FALSE;++count;
    if (trace && lineIsInst(p,"ecall")) {
      const char *name=NULL;unsigned bytes=0;bool eligible=m251CallRegionCallee(p,&name,&bytes);
      fprintf(stderr,"call-region site=%s eligible=%d stack=%u ic=%d\n",p->line,eligible,bytes,p->ic->op);
    }
  }
  m251_call_sequence *a=Safe_alloc(sizeof(*a)),*b=Safe_alloc(sizeof(*b)),*best=Safe_alloc(sizeof(*best));
  for (;;) {
    unsigned bestStart=0,bestLength=0,bestCopies=0,bestGain=127;
    for (unsigned i=0;i<count;++i) {
      if (lines[i].used) continue;
      for (unsigned length=24;length<=M251_OUTLINE_MAX && i+length<=count;++length) {
        if (!m251CallRegionSequence(lines,i,length,a,scratch)) continue;
        unsigned copies=0;bool different=FALSE;
        for (unsigned j=i;j+length<=count;) {
          if (m251CallRegionSequence(lines,j,length,b,scratch) && m251CallRegionEqual(a,b)) {
            ++copies;different|=strcmp(a->callee,b->callee)!=0;j+=length;
          } else ++j;
        }
        /* 每条原指令至少 1B；每站 MOV/MOVH/ECALL 12B，ERET 1B。
         * SPX 无位移转 indexed 的增长另扣；不把指令数当周期。 */
        unsigned overhead=12*copies+1+a->growth;
        /* 每个准入区域恰有一条已核实的 ECALL symbol（4 B）。
         * 不能把它按 1 B 计价，让删掉搬运的区域意外失去共享资格。 */
        /* 新物理首参试验内启用精确的 CALL 下界；其它配置保留封存策略，
         * 不把独立候选的共享变化扩散到尚未重新实板核对的工程。 */
        unsigned minBytes=length+(a->internalArgument?3:0);
        unsigned gain=copies>=4 && (copies-1)*minBytes>overhead ? (copies-1)*minBytes-overhead : 0;
        if (different && gain>bestGain) {bestGain=gain;bestStart=i;bestLength=length;bestCopies=copies;*best=*a;}
      }
    }
    if (!bestLength) break;
    symbol *label=newiTempLabel(NULL);char target[64];SNPRINTF(target,sizeof target,"%05d$",labelKey2num(label->key));
    dbuf_printf(helpers,"; MX251 call region: %u instructions, %u copies, lower saving %u; extra stack 3; target DR%u\n%s:\n",bestLength,bestCopies,bestGain,scratch,target);
    for (unsigned k=0;k<bestLength;++k) dbuf_printf(helpers,"\t%s\n",best->body[k]);
    dbuf_append_str(helpers,"\teret\n");
    for (unsigned j=bestStart;j+bestLength<=count;) {
      if (!m251CallRegionSequence(lines,j,bestLength,b,scratch) || !m251CallRegionEqual(best,b)) {++j;continue;}
      for (unsigned k=0;k<bestLength;++k) {
        lineNode *p=lines[j+k].node;char text[M251_OUTLINE_TEXT];
        /* DR 符号立即数可能触发汇编器的错误扩展；低半用固定宽度 WR 重定位。 */
        if (!k) snprintf(text,sizeof text,"mov wr%u,#%s",scratch+2,b->callee);
        else if (k==1) snprintf(text,sizeof text,"movh dr%u,#(%s >> 16)",scratch,b->callee);
        else if (k==2) snprintf(text,sizeof text,"ecall %s",target);
        else strcpy(text,"; MX251 shared call instruction");
        p->line=Safe_strdup(text);p->isComment=k>2;p->aln=NULL;lines[j+k].used=TRUE;
      }
      j+=bestLength;
    }
  }
  Safe_free(best);Safe_free(b);Safe_free(a);Safe_free(lines);
}
