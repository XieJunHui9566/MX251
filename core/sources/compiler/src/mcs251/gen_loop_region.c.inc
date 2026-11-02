/* GPL-2.0-or-later. 有界嵌套调用区域共享；实验开关默认关闭。
 * 此处只识别已发射的有限计数循环，不按函数名、测试输入或器件名选择。
 * 两层循环及外设访问顺序均保留；对象的最后值在返回原栈帧后写回。 */
#define M251_OUTER_MAX_LINES 8192
#define M251_OUTER_MAX_COPIES 128
typedef struct { lineNode *node; char text[M251_OUTLINE_TEXT]; } m251_outer_line;
typedef struct {
  unsigned begin, index, control, inner, call, store, collect, tail, end;
  unsigned sampleReg, indexReg, counter, saved, result, returned, base, innerLimit, outerLimit, argBytes;
  bool innerHighByte, nativeDual;
  int highSlot, lowSlot;
  symbol *object;
  char outerLabel[64], innerLabel[64], checkLabel[64], exitLabel[64], collector[64], target[256];
} m251_outer_region;

static void m251OuterNormalize(const char *s,char *out)
{
  unsigned n=0;
  for (;*s && *s!=';';++s) if (!isspace((unsigned char)*s)) {
    if (n+1>=M251_OUTLINE_TEXT) {out[0]=0;return;} out[n++]=*s;
  }
  out[n]=0;
}
static bool m251OuterMatch1(const char *s,const char *fmt,unsigned *a)
{ int n=0;return sscanf(s,fmt,a,&n)==1 && n>0 && !s[n]; }
static bool m251OuterMatch2(const char *s,const char *fmt,unsigned *a,unsigned *b)
{
  int n=0;
  if(strstr(fmt,"%i")) {
    int literal=0;if(sscanf(s,fmt,a,&literal,&n)!=2 || n<=0 || s[n] || literal<0)return FALSE;
    *b=(unsigned)literal;return TRUE;
  }
  return sscanf(s,fmt,a,b,&n)==2 && n>0 && !s[n];
}
static bool m251OuterImm(const char *s,const char *prefix,unsigned *v)
{
  char *end;unsigned long x;if(strncmp(s,prefix,strlen(prefix)))return FALSE;
  s+=strlen(prefix);if(!*s)return FALSE;x=strtoul(s,&end,0);
  if(*end || x>65535)return FALSE;*v=(unsigned)x;return TRUE;
}
static bool m251OuterLabel(const m251_outer_line *l,char *out)
{
  size_t n=strlen(l->text);if(!l->node->isLabel || n<2 || n>=64 || l->text[n-1]!=':')return FALSE;
  memcpy(out,l->text,n-1);out[n-1]=0;return TRUE;
}
static bool m251OuterBranch(const char *s,const char *op,const char *label)
{return !strncmp(s,op,strlen(op)) && !strcmp(s+strlen(op),label);}
static bool m251OuterSlot(const char *s,bool store,int *offset,unsigned *reg)
{
  char a[64],b[64],*end;int n=0;long off=0;
  if(sscanf(s,"mov%63[^,],%63s%n",a,b,&n)!=2 || s[n])return FALSE;
  const char *slot=store?a:b,*view=store?b:a;
  if(strncmp(slot,"@spx",4) || !m251OuterMatch1(view,"wr%u%n",reg))return FALSE;
  if(slot[4]) {if(slot[4]!='-' && slot[4]!='+')return FALSE;off=strtol(slot+4,&end,0);if(*end)return FALSE;}
  if(off < -32768 || off>0)return FALSE;*offset=(int)off;return TRUE;
}
static symbol *m251OuterStoredObject(lineNode *a,lineNode *b)
{
  iCode *ic=a->ic;operand *op;
  if(!ic || ic!=b->ic || ic->op!='=' || POINTER_SET(ic))return NULL;
  op=IC_RESULT(ic);
  if(!IS_TRUE_SYMOP(op) || isOperandVolatileOrAtomic(op,TRUE))return NULL;
  symbol *s=OP_SYMBOL(op);
  if(!s->onStack || getSize(s->type)!=4 || !IS_SPEC(s->type) || IS_FLOAT(s->type) ||
     IS_STATIC(s->type) || !s->level)return NULL;
  return s;
}
/* SFR 寄存器必须来自绝对地址 IR；SP/PSW 不允许作为被挪动区域的源级状态。 */
static bool m251OuterSfrWrite(lineNode *p)
{
  const char *name=NULL;int bit=0;
  if(!p->ic || p->isInline || !m251OutlineReadSfr(IC_RESULT(p->ic),&name,&bit))return FALSE;
  char t[M251_OUTLINE_TEXT];m251OuterNormalize(p->line,t);
  if(bit)return m251OuterBranch(t,"clr",name)||m251OuterBranch(t,"setb",name);
  char prefix[300];unsigned v;snprintf(prefix,sizeof prefix,"mov%s,#",name);
  return m251OuterImm(t,prefix,&v) && v<=255;
}
/* ABI4 单独证明，不放宽既有 ABI2 单调用区域规则。只接当前完整镜像
 * V2 契约确认的双整型寄存器槽；函数指针、浮点、窄值和栈实参仍拒绝。 */
static bool m251OuterNativeCallee(lineNode *p,const char **name)
{
  if(!p->ic || p->isInline || p->ic->op!=CALL || !IS_SYMOP(IC_LEFT(p->ic)))return FALSE;
  symbol *callee=OP_SYMBOL(IC_LEFT(p->ic));sym_link *t=operandType(IC_LEFT(p->ic));
  if(!IS_FUNC(t) || FUNC_SDCCCALL(t)!=4 || !mcs251InternalNative(callee) ||
     !IFFUNC_ISREENT(t) || IFFUNC_HASVARARGS(t) || IFFUNC_ISNAKED(t) ||
     IFFUNC_CALLEESAVES(t) || IFFUNC_ISISR(t) || IFFUNC_ISCRITICAL(t) ||
     IFFUNC_ISBANKEDCALL(t) || FUNC_REGBANK(t) || SPEC_NORETURN(getSpec(t)) ||
     p->ic->parmBytes!=0 || !m251CallRegionScalar(t->next) ||
     !IS_INTEGRAL(t->next) || getSize(t->next)!=4)return FALSE;
  unsigned count=0;
  for(value *v=FUNC_ARGS(t);v;v=v->next) {
    if(++count>2 || !m251CallRegionScalar(v->type) || !IS_INTEGRAL(v->type) || getSize(v->type)!=4)return FALSE;
  }
  if(count!=2)return FALSE;
  char expected[M251_OUTLINE_TEXT],normalized[M251_OUTLINE_TEXT];
  m251OuterNormalize(p->line,normalized);snprintf(expected,sizeof expected,"ecall%s",callee->rname);
  if(strcmp(normalized,expected))return FALSE;
  *name=callee->rname;return TRUE;
}
/* genMove 的原生纯字面量传送可按低字优先或高字优先发射。
 * 逐条要求 WR4/6/12/14 各写一次且仅写 16 位常量，保持原来的顺序。
 * 不能将任意 MOV 串误当作寄存器参数，更不能跨入内存/volatile 读取。 */
static bool m251OuterNativeLiterals(m251_outer_line *l,unsigned count,unsigned start)
{
  const char *enabled=getenv("MX251_NATIVE_LITERAL_WORD");
  if(!enabled || strcmp(enabled,"1") || start+4>count)return FALSE;
  unsigned seen=0;
  for(unsigned i=0;i<4;++i) {
    unsigned reg,imm,bit;
    if(!m251OuterMatch2(l[start+i].text,"movwr%u,#%i%n",&reg,&imm) || imm>65535 ||
       l[start+i].node->isLabel || l[start+i].node->isInline)return FALSE;
    if(reg==4)bit=1;else if(reg==6)bit=2;else if(reg==12)bit=4;else if(reg==14)bit=8;else return FALSE;
    if(seen&bit)return FALSE;seen|=bit;
  }
  return seen==15;
}
static bool m251OuterParse(m251_outer_line *l,unsigned count,unsigned start,m251_outer_region *r)
{
  unsigned z,a,b,k,reg,imm;char prefix[64];int n=0;
  memset(r,0,sizeof(*r));r->begin=start;
  if(start+35>=count || !m251OuterMatch2(l[start].text,"movr%u,#%i%n",&r->sampleReg,&z) || z || r->sampleReg>7 ||
     !m251OuterLabel(l+start+1,r->outerLabel))return FALSE;
  k=start+2;r->index=k;
  if(m251OuterMatch2(l[k].text,"movar%u,r%u%n",&r->indexReg,&a) || m251OuterMatch2(l[k].text,"movr%u,r%u%n",&r->indexReg,&a)) {
    if(a!=r->sampleReg)return FALSE;++k;
    while(k<count && m251OuterMatch1(l[k].text,"incr%u%n",&a) && a==r->indexReg) {++r->base;++k;if(r->base>255)return FALSE;}
    if(k+2<count && m251OuterImm(l[k].text,"mova,#",&imm)) {
      if(imm>255 || !m251OuterMatch1(l[k+1].text,"adda,r%u%n",&a) || a!=r->indexReg ||
         !m251OuterMatch1(l[k+2].text,"movr%u,a%n",&a) || a!=r->indexReg)return FALSE;
      r->base=(r->base+imm)&255;k+=3;
    }
  } else {
    if(!m251OuterMatch1(l[k++].text,"mova,r%u%n",&a) || a!=r->sampleReg ||
       !m251OuterImm(l[k++].text,"adda,#",&r->base) || r->base>255 ||
       !m251OuterMatch1(l[k++].text,"movr%u,a%n",&r->indexReg))return FALSE;
  }
  if(r->indexReg>7 || r->indexReg==r->sampleReg || k+40>=count)return FALSE;
  r->control=k;
  for(z=0;z<4;++z)if(!m251OuterSfrWrite(l[k+z].node))return FALSE;
  k+=4;
  if(!m251OuterMatch2(l[k++].text,"movr%u,#%i%n",&a,&imm) || imm ||
     !m251OuterMatch2(l[k++].text,"movr%u,#%i%n",&r->counter,&imm) || imm || a!=r->counter+1 ||
     r->counter>14 || (r->counter&1) || !m251OuterLabel(l+k,r->innerLabel))return FALSE;
  r->inner=++k;
  if(!m251OuterMatch1(l[k++].text,"pushdr%u%n",&r->saved) || (r->saved&3) || r->saved>12 ||
     r->counter<r->saved || r->counter+1>r->saved+3 ||
     (r->sampleReg>=r->counter && r->sampleReg<=r->counter+1) ||
     (r->indexReg>=r->counter && r->indexReg<=r->counter+1) ||
     r->sampleReg<r->saved || r->sampleReg>r->saved+3 ||
     r->indexReg<r->saved || r->indexReg>r->saved+3)return FALSE;
  if(r->saved==4 && m251OuterNativeLiterals(l,count,k)) {r->nativeDual=TRUE;k+=4;}
  else {
    while(k<count && m251OuterImm(l[k].text,"pushw#",&imm)) {r->argBytes+=2;++k;if(r->argBytes>16)return FALSE;}
    if(!r->argBytes)return FALSE;
    /* 此有限 lowering 只支持常量栈实参和常量首参；任意栈读不跨新帧。 */
    if(!m251OuterImm(l[k++].text,"mova,#",&imm) || imm>255 ||
       !m251OuterMatch2(l[k++].text,"movwr%u,#%i%n",&a,&imm) ||
       !m251OuterMatch2(l[k++].text,"movwr%u,#%i%n",&b,&imm) || a!=r->saved || b!=a+2)return FALSE;
  }
  r->call=k;
  const char *name=NULL;unsigned bytes=0;
  if(r->nativeDual) {
    if(!m251OuterNativeCallee(l[k].node,&name) || strlen(name)>=sizeof r->target)return FALSE;
  } else if(!m251CallRegionCallee(l[k].node,&name,&bytes) || bytes!=r->argBytes ||
            !mcs251InternalArgument(OP_SYMBOL(IC_LEFT(l[k].node->ic))) ||
            !mcs251InternalReturn(OP_SYMBOL(IC_LEFT(l[k].node->ic))) || strlen(name)>=sizeof r->target)return FALSE;
  strcpy(r->target,name);++k;
  if(!m251OuterMatch2(l[k++].text,"movdr%u,dr%u%n",&r->result,&r->returned) || (r->result&3) ||
     r->result>12 || r->result==r->saved || r->returned!=r->saved ||
     (r->nativeDual && r->result!=12))return FALSE;
  if(!r->nativeDual && (!m251OuterMatch1(l[k++].text,"decspx,#%u%n",&a) || a!=r->argBytes))return FALSE;
  if(!m251OuterMatch1(l[k++].text,"popdr%u%n",&a) || a!=r->saved)return FALSE;
  r->store=k;
  if(!m251OuterSlot(l[k++].text,TRUE,&r->highSlot,&a) || a!=r->result+2 ||
     !m251OuterSlot(l[k++].text,TRUE,&r->lowSlot,&a) || a!=r->result || r->highSlot!=r->lowSlot+2 ||
     !(r->object=m251OuterStoredObject(l[r->store].node,l[r->store+1].node)))return FALSE;
  if(!m251OuterMatch2(l[k++].text,"incwr%u,#%u%n",&a,&b) || a!=r->counter || b!=1)return FALSE;
  snprintf(prefix,sizeof prefix,"cmpwr%u,#",r->counter);
  if(m251OuterImm(l[k].text,prefix,&r->innerLimit)) {
    if(!r->innerLimit || !m251OuterBranch(l[k+1].text,"jc",r->innerLabel))return FALSE;
    k+=2;
  } else {
    /* 已有 lowering 对正的 256 整数倍界限只比较 WR 的高字节。
     * 从零开始、单位递增且阈值为 1..255，进位恰好表示已到达上界。
     * 保留原 MOV/ADD/JNC，不把字节比较替换成另一种周期或 FLAGS 行为。 */
    if(!m251OuterImm(l[k].text,"mova,#0x100-",&imm) || !imm || imm>255 ||
       !m251OuterMatch1(l[k+1].text,"adda,r%u%n",&a) || a!=r->counter ||
       !m251OuterBranch(l[k+2].text,"jnc",r->innerLabel))return FALSE;
    r->innerHighByte=TRUE;r->innerLimit=imm*256;k+=3;
  }
  if(!m251OuterSfrWrite(l[k].node))return FALSE;
  ++k;r->collect=k;
  if(strncmp(l[k].text,"ecall",5) || strlen(l[k].text+5)>=sizeof r->collector)return FALSE;
  strcpy(r->collector,l[k++].text+5);r->tail=k;
  int sampleBound=0;
  if(sscanf(l[k++].text,"cjner%u,#%i,%63[^\n]%n",&reg,&sampleBound,r->checkLabel,&n)!=3 ||
     n<=0 || l[k-1].text[n] || reg!=r->sampleReg || sampleBound<1 || sampleBound>255)return FALSE;
  r->outerLimit=(unsigned)sampleBound;
  char label[64];if(!m251OuterLabel(l+k++,label) || strcmp(label,r->checkLabel))return FALSE;
  if(strncmp(l[k].text,"jnc",3) || strlen(l[k].text+3)>=sizeof r->exitLabel)return FALSE;
  strcpy(r->exitLabel,l[k++].text+3);
  if(!m251OuterBranch(l[k++].text,"ejmp",r->outerLabel) || !m251OuterLabel(l+k,label) || strcmp(label,r->exitLabel))return FALSE;
  r->end=k-1;
  return TRUE;
}

static bool m251OuterSame(m251_outer_line *l,const m251_outer_region *a,const m251_outer_region *b)
{
  if(a->nativeDual!=b->nativeDual || a->sampleReg!=b->sampleReg || a->indexReg!=b->indexReg || a->counter!=b->counter || a->saved!=b->saved ||
     a->result!=b->result || a->returned!=b->returned || a->innerLimit!=b->innerLimit || a->innerHighByte!=b->innerHighByte || a->outerLimit!=b->outerLimit ||
     a->argBytes!=b->argBytes || a->object!=b->object || a->highSlot!=b->highSlot || a->lowSlot!=b->lowSlot ||
     strcmp(a->collector,b->collector) || a->call-a->control!=b->call-b->control)return FALSE;
  for(unsigned j=0;j<a->call-a->control;++j) {
    unsigned x=a->control+j,y=b->control+j;
    if(l[x].node->isLabel && l[y].node->isLabel)continue;
    if(strcmp(l[x].text,l[y].text))return FALSE;
  }
  return TRUE;
}
static bool m251OuterToken(const char *text,const char *token)
{
  size_t n=strlen(token);for(const char *p=text;(p=strstr(p,token))!=NULL;++p)
    if((p==text || !isalnum((unsigned char)p[-1])) && !isalnum((unsigned char)p[n]) && p[n]!='_')return TRUE;
  return FALSE;
}
/* 普通分配器不分配 DR24。仍核对实际发射文本：既有共享调用只准许
 * 紧邻的完整目标定义+调用，不允许其它读写带着 scratch 活跃值穿过区域。 */
static bool m251OuterScratch(m251_outer_line *l,unsigned count,const char *helpers)
{
  for(unsigned i=0;i<count;++i) {
    const char *s=l[i].text;
    if(!strstr(s,"dr24") && !strstr(s,"wr24") && !strstr(s,"wr26"))continue;
    if(strncmp(s,"movwr26,#",9) || i+2>=count || strncmp(l[i+2].text,"ecall",5))return FALSE;
    char high[M251_OUTLINE_TEXT];snprintf(high,sizeof high,"movhdr24,#(%s>>16)",s+9);
    if(strcmp(high,l[i+1].text))return FALSE;
    char label[96];snprintf(label,sizeof label,"%s:\n",l[i+2].text+5);
    const char *body=strstr(helpers,label),*end;
    if(!body || (body!=helpers && body[-1]!='\n') || !(end=strstr(body,"\teret\n")))return FALSE;
    unsigned uses=0;
    for(const char *p=body;p<end;) {
      const char *q=strchr(p,'\n');char raw[M251_OUTLINE_TEXT],text[M251_OUTLINE_TEXT];
      if(!q || (size_t)(q-p)>=sizeof raw)return FALSE;
      memcpy(raw,p,q-p);raw[q-p]=0;m251OuterNormalize(raw,text);
      if(strstr(text,"dr24") || strstr(text,"wr24") || strstr(text,"wr26")) {
        if(strcmp(text,"ecall@dr24"))return FALSE;++uses;
      }
      p=q+1;
    }
    if(uses!=1)return FALSE;
    ++i;
  }
  return TRUE;
}
/* 任何跳入循环内部的引用（包括非分支取址）都使单入口证明失败。 */
static bool m251OuterSingleEntry(m251_outer_line *l,unsigned count,const m251_outer_region *r,const char *helpers)
{
  const char *labels[3]={r->outerLabel,r->innerLabel,r->checkLabel};
  for(unsigned j=0;j<3;++j) {
    if(strstr(helpers,labels[j]))return FALSE;
    for(unsigned i=0;i<count;++i)if((i<r->begin || i>r->end) && m251OuterToken(l[i].node->line,labels[j]))return FALSE;
  }
  return TRUE;
}
static bool m251OuterObjectOperand(operand *op,symbol *s)
{return IS_SYMOP(op) && isSymbolEqual(OP_SYMBOL(op),s);}
static int m251OuterFindIR(iCode **ir,unsigned n,symbol *label)
{
  if(!label)return -1;for(unsigned i=0;i<n;++i)if(ir[i]->op==LABEL && IC_LABEL(ir[i])==label)return (int)i;return -1;
}
/* source ADDRESS_OF 到待共享区域不存在路径，才允许忽略每轮不可观察的栈写。
 * 后面的取址不自动否决；若有回边重新进入区域则仍拒绝。 */
static bool m251OuterUnexposed(iCode *lic,m251_outer_line *l,m251_outer_region *r,unsigned copies,const char **why)
{
  if(!lic || !copies){*why="missing-ir-chain";return FALSE;}
  unsigned n=0;for(iCode *ic=lic;ic;ic=ic->next)if(++n>M251_OUTER_MAX_LINES){*why="ir-capacity";return FALSE;}
  iCode **ir=Safe_alloc(n*sizeof(*ir));unsigned *queue=Safe_alloc(n*sizeof(*queue));bool *seen=Safe_alloc(n*sizeof(*seen)),*inside=Safe_alloc(n*sizeof(*inside));
  unsigned i=0;for(iCode *ic=lic;ic;ic=ic->next)ir[i++]=ic;
  memset(inside,0,n*sizeof(*inside));bool ok=TRUE;
  for(unsigned c=0;c<copies;++c)for(lineNode *p=l[r[c].begin].node;p && p!=l[r[c].end].node->next;p=p->next) {
    if(p->ic)for(i=0;i<n;++i)if(ir[i]==p->ic)inside[i]=TRUE;
  }
  for(unsigned c=0;c<copies && ok;++c) {
    bool callFound=FALSE,storeFound=FALSE;
    for(i=0;i<n;++i) {if(ir[i]==l[r[c].call].node->ic)callFound=TRUE;if(ir[i]==l[r[c].store].node->ic)storeFound=TRUE;}
    if(!callFound || !storeFound){*why="region-ir-not-in-chain";ok=FALSE;}
    for(lineNode *p=l[r[c].collect].node;p && p!=l[r[c].tail].node;p=p->next)if(p->ic) {
      bool found=FALSE;for(i=0;i<n;++i)if(ir[i]==p->ic)found=TRUE;
      if(!found){*why="collector-ir-not-in-chain";ok=FALSE;break;}
    }
  }
  for(i=0;i<n && ok;++i) {
    iCode *ic=ir[i];
    if(ic->op==INLINEASM || ic->op==JUMPTABLE || ic->op==CRITICAL || ic->op==ENDCRITICAL){*why="unmodeled-ir-control";ok=FALSE;break;}
    /* localEscapeAlive 是任意局部的合并标记，不能证明当前对象已逃逸。
     * 当前对象由 ADDRESS_OF 的全部 CFG 可达路径单独核对。 */
    if(inside[i] && (m251OuterObjectOperand(IC_LEFT(ic),r[0].object) ||
       m251OuterObjectOperand(IC_RIGHT(ic),r[0].object) || m251OuterObjectOperand(IC_RESULT(ic),r[0].object))) {
      bool allowed=FALSE;
      for(unsigned c=0;c<copies;++c) {
        if(ic==l[r[c].store].node->ic)allowed=TRUE;
        for(lineNode *p=l[r[c].collect].node;p && p!=l[r[c].tail].node;p=p->next)
          if(p->ic==ic && !m251OuterObjectOperand(IC_RESULT(ic),r[0].object) && ic->op!=ADDRESS_OF)allowed=TRUE;
      }
      if(!allowed){*why="object-other-region-use";ok=FALSE;break;}
    }
    if(ic->op!=ADDRESS_OF || !m251OuterObjectOperand(IC_LEFT(ic),r[0].object))continue;
    memset(seen,0,n*sizeof(*seen));unsigned take=0,put=0;queue[put++]=i;seen[i]=TRUE;
    while(take<put && ok) {
      unsigned at=queue[take++];iCode *x=ir[at];int next[3]={(int)at+1,-1,-1};
      if(inside[at]){*why="object-address-reaches-region";ok=FALSE;break;}
      if(x->op==GOTO){next[0]=m251OuterFindIR(ir,n,IC_LABEL(x));if(next[0]<0){*why="unknown-goto";ok=FALSE;break;}}
      else if(x->op==IFX) {
        if(IC_TRUE(x)){next[1]=m251OuterFindIR(ir,n,IC_TRUE(x));if(next[1]<0){*why="unknown-true-edge";ok=FALSE;break;}}
        if(IC_FALSE(x)){next[2]=m251OuterFindIR(ir,n,IC_FALSE(x));if(next[2]<0){*why="unknown-false-edge";ok=FALSE;break;}}
      } else if(x->op==RETURN || x->op==ENDFUNCTION)next[0]=-1;
      for(unsigned e=0;e<3;++e)if(next[e]>=0 && (unsigned)next[e]<n && !seen[next[e]]){seen[next[e]]=TRUE;queue[put++]=(unsigned)next[e];}
    }
  }
  Safe_free(inside);Safe_free(seen);Safe_free(queue);Safe_free(ir);return ok;
}

/* 用物理字节交叠检查冷 helper 的 live-through 值；对隐式 A/B 写保持保守。 */
static unsigned long m251OuterRegMask(const char *s)
{
  if(*s=='@')++s;
  unsigned r,w=1;if(!strcmp(s,"a"))return 1UL<<11;if(!strcmp(s,"b"))return 1UL<<10;
  if(!strcmp(s,"ab"))return 3UL<<10;
  if(s[0]=='d' && s[1]=='r'){w=4;s+=1;}else if(s[0]=='w' && s[1]=='r'){w=2;s+=1;}else if(s[0]=='a' && s[1]=='r')s+=1;
  if(!m251OuterMatch1(s,"r%u%n",&r) || r+w>32)return 0;
  return ((1UL<<w)-1)<<r;
}
static bool m251OuterCollector(const char *helpers,const m251_outer_region *r,unsigned copies,struct dbuf_s *rewritten,const char **why)
{
  char label[80];snprintf(label,sizeof label,"%s:\n",r->collector);
  const char *start=strstr(helpers,label);if(!start || (start!=helpers && start[-1]!='\n')){*why="collector-not-local-outline";return FALSE;}
  const char *end=strstr(start,"\teret\n");if(!end){*why="collector-no-return";return FALSE;}end+=strlen("\teret\n");
  const char *header=start;if(header>helpers && header[-1]=='\n')--header;
  while(header>helpers && header[-1]!='\n')--header;
  if(strncmp(header,"; MX251 outline ",16)){*why="collector-missing-origin-header";return FALSE;}
  unsigned long valueMask=15UL<<r->result;unsigned loads=0,sampleIncrements=0,instructions=0;bool good=TRUE;
  dbuf_append(rewritten,helpers,header-helpers);
  dbuf_append_str(rewritten,"; MX251 outer collector: register-forwarded value; contract below\n");
  for(const char *p=start;p<end;) {
    const char *q=strchr(p,'\n');if(!q || q>=end)q=end-1;
    size_t len=q-p;char raw[M251_OUTLINE_TEXT],text[M251_OUTLINE_TEXT];
    if(len>=sizeof raw){*why="collector-line-capacity";return FALSE;}memcpy(raw,p,len);raw[len]=0;m251OuterNormalize(raw,text);
    if(*text && text[strlen(text)-1]==':' && p!=start){*why="collector-interior-entry";return FALSE;}
    int off;unsigned reg;
    if(strstr(text,"@spx")) {
      if(!m251OuterSlot(text,FALSE,&off,&reg) || loads>=2 ||
         reg!=(loads?r->result:r->result+2) || off!=(loads?r->lowSlot:r->highSlot)-3) {good=FALSE;*why="collector-stack-contract";break;}
      ++loads;
    } else {
      if(strstr(text,"spx")){good=FALSE;*why="collector-unmodeled-stack-reference";break;}
      if(*text && text[strlen(text)-1]!=':' && strcmp(text,"eret")) {
        ++instructions;
        char op[16],args[M251_OUTLINE_TEXT];unsigned z=0;const char *t=raw;
        while(isspace((unsigned char)*t))++t;while(isalpha((unsigned char)*t) && z+1<sizeof op)op[z++]=*t++;op[z]=0;
        if(strcmp(op,"mov") && strcmp(op,"movz") && strcmp(op,"movs") && strcmp(op,"movc") &&
           strcmp(op,"add") && strcmp(op,"addc") && strcmp(op,"sub") && strcmp(op,"subb") &&
           strcmp(op,"anl") && strcmp(op,"orl") && strcmp(op,"xrl") && strcmp(op,"cmp") &&
           strcmp(op,"inc") && strcmp(op,"dec") && strcmp(op,"clr") && strcmp(op,"cpl") &&
           strcmp(op,"rl") && strcmp(op,"rlc") && strcmp(op,"rr") && strcmp(op,"rrc") &&
           strcmp(op,"swap") && strcmp(op,"mul") && strcmp(op,"div")) {
          good=FALSE;*why="collector-unmodeled-instruction";break;
        }
        m251OuterNormalize(t,args);char *comma=strchr(args,',');if(comma)*comma++=0;
        unsigned long dst=m251OuterRegMask(args),src=comma?m251OuterRegMask(comma):0;
        if(!strcmp(op,"mul") || !strcmp(op,"div"))dst|=3UL<<10;
        if(dst & (1UL<<r->sampleReg)) {
          char counter[16];snprintf(counter,sizeof counter,"r%u",r->sampleReg);
          if(strcmp(op,"inc") || strcmp(args,counter) || comma){good=FALSE;*why="collector-counter-write";break;}
          ++sampleIncrements;
        }
        if((dst&valueMask) || (loads<2 && (src&valueMask)) || strstr(text,"ecall") || strstr(text,"ejmp")) {
          good=FALSE;*why="collector-value-not-live-through";break;
        }
      }
      dbuf_append(rewritten,p,len+1);
    }
    p=q+1;
  }
  if(!good || loads!=2 || sampleIncrements!=1){if(good)*why=loads!=2?"collector-load-count":"collector-counter-step";return FALSE;}
  dbuf_printf(rewritten,"; MX251 outer-collector {\"schema\":1,\"label\":\"%s\",\"instructions\":%u,\"sourceInstructions\":%u,\"removedLoads\":2,\"oldPhysicalCallers\":%u,\"newPhysicalCallers\":1,\"valueReg\":%u,\"highSlot\":%d,\"lowSlot\":%d}\n",r->collector,instructions,instructions+2,copies,r->result,r->highSlot-3,r->lowSlot-3);
  dbuf_append_str(rewritten,end);return TRUE;
}

/* 仅删除相邻、无其它入口的下一区域必定重定义之前的最终写回。
 * 连续段最后一个区域始终写回，因此不推断任意后继路径的活跃性。 */
static bool m251OuterFinalStoreDead(m251_outer_line *l,unsigned count,const m251_outer_region *a,
                                    const m251_outer_region *b,const char *helpers)
{
  if(a->end+2!=b->begin || !isSymbolEqual(a->object,b->object) || !b->innerLimit || !b->outerLimit ||
     strcmp(a->collector,b->collector) || strstr(helpers,a->exitLabel))return FALSE;
  for(unsigned i=0;i<count;++i)
    if((i<a->begin || i>a->end+1) && m251OuterToken(l[i].node->line,a->exitLabel))return FALSE;
  return TRUE;
}

#include "gen_outer_counter.c.inc"

static void mcs251OutlineOuterCallRegions(lineNode *head,struct dbuf_s *helpers,iCode *lic)
{
  const char *enable=getenv("MX251_OUTER_CALL_REGION");if(!enable || strcmp(enable,"1"))return;
  bool trace=getenv("MX251_OUTER_CALL_REGION_TRACE")!=NULL;const char *why="no-finite-region";
  if(!currFunc || options.useXstack || FUNC_SDCCCALL(currFunc->type)!=2 || IFFUNC_ISISR(currFunc->type) ||
     IFFUNC_ISNAKED(currFunc->type) || IFFUNC_CALLEESAVES(currFunc->type) || FUNC_REGBANK(currFunc->type))return;
  unsigned count=0;for(lineNode *p=head;p;p=p->next) {
    if(p->isInline)return;if(!p->isComment && !p->isDebug)++count;
  }
  if(count>M251_OUTER_MAX_LINES)return;
  m251_outer_line *l=Safe_alloc(count*sizeof(*l));unsigned at=0;
  for(lineNode *p=head;p;p=p->next)if(!p->isComment && !p->isDebug){l[at].node=p;m251OuterNormalize(p->line,l[at++].text);}
  m251_outer_region *regions=Safe_alloc(M251_OUTER_MAX_COPIES*sizeof(*regions));unsigned copies=0;
  if(!m251OuterScratch(l,count,dbuf_c_str(helpers))){why="scratch-live-or-unmodeled";goto reject;}
  for(unsigned i=0;i<count && copies<M251_OUTER_MAX_COPIES;++i) {
    m251_outer_region r;if(!m251OuterParse(l,count,i,&r))continue;
    if(!copies || m251OuterSame(l,regions,&r)){regions[copies++]=r;i=r.end;}
  }
  if(copies<4){why="insufficient-equal-regions";goto reject;}
  const char *old=dbuf_c_str(helpers);m251_outer_region *r=regions;
  for(unsigned i=0;i<copies;++i)if(!m251OuterSingleEntry(l,count,regions+i,old)){why="external-region-entry";goto reject;}
  /* 每个 helper 调用点必须属于本次已验证区域；不得局部修改共享契约。 */
  unsigned calls=0;
  for(unsigned i=0;i<count;++i)if(m251OuterToken(l[i].node->line,r->collector)) {
    bool owned=FALSE;for(unsigned c=0;c<copies;++c)if(i==regions[c].collect)owned=TRUE;
    if(!owned){why="collector-other-reference";goto reject;}++calls;
  }
  if(calls!=copies){why="collector-call-count";goto reject;}
  const char *p=old;unsigned definitions=0;
  while((p=strstr(p,r->collector))!=NULL){if(p[strlen(r->collector)]!=':'){why="collector-helper-reference";goto reject;}++definitions;p+=strlen(r->collector);}
  if(definitions!=1){why="collector-not-unique";goto reject;}
  if(!m251OuterUnexposed(lic,l,regions,copies,&why))goto reject;
  struct dbuf_s changed;dbuf_init(&changed,dbuf_get_length(helpers)+2048);
  if(!m251OuterCollector(old,r,copies,&changed,&why)){dbuf_destroy(&changed);goto reject;}
  if(m251OuterCounterApply(l,count,regions,copies,old,&changed,&why)) {
    dbuf_set_length(helpers,0);dbuf_append(helpers,dbuf_get_buf(&changed),dbuf_get_length(&changed));dbuf_destroy(&changed);
    if(trace)fprintf(stderr,"outer-counter accepted function=%s copies=%u\n",currFunc->name,copies);
    Safe_free(regions);Safe_free(l);return;
  }
  if(trace && getenv("MX251_OUTER_COUNTER"))fprintf(stderr,"outer-counter fallback function=%s reason=%s\n",currFunc->name,why);
  symbol *s0=newiTempLabel(NULL),*s1=newiTempLabel(NULL),*s2=newiTempLabel(NULL),*s3=newiTempLabel(NULL),*s4=newiTempLabel(NULL),*s5=newiTempLabel(NULL);
  char entry[64],outer[64],inner[64],check[64],done[64],returnLabel[64];
  snprintf(entry,sizeof entry,"%05d$",labelKey2num(s0->key));snprintf(outer,sizeof outer,"%05d$",labelKey2num(s1->key));
  snprintf(inner,sizeof inner,"%05d$",labelKey2num(s2->key));snprintf(check,sizeof check,"%05d$",labelKey2num(s3->key));snprintf(done,sizeof done,"%05d$",labelKey2num(s4->key));
  snprintf(returnLabel,sizeof returnLabel,"%05d$",labelKey2num(s5->key));
  /* target 4B + base 1B 是 helper 私有局部；内层原 save 4B + 实参决定重载位移。 */
  unsigned targetHigh=1+4+r->argBytes+3,targetLow=targetHigh-2;
  dbuf_printf(&changed,"; MX251 outer-region {\"schema\":1,\"entry\":\"%s\",\"copies\":%u,\"collector\":\"%s\",\"sampleReg\":%u,\"indexReg\":%u,\"counter\":%u,\"saved\":%u,\"result\":%u,\"innerLimit\":%u,\"innerCompare\":\"%s\",\"outerLimit\":%u,\"argBytes\":%u,\"calleeABI\":%u,\"argumentRegisters\":%s,\"returnRegister\":%u,\"stackDelta\":8,\"targetHigh\":%u,\"targetLow\":%u,\"highSlot\":%d,\"lowSlot\":%d,\"finalStorePolicy\":\"last-adjacent-region\",\"cleanupFlagReplay\":true}\n",
    entry,copies,r->collector,r->sampleReg,r->indexReg,r->counter,r->saved,r->result,r->innerLimit,r->innerHighByte?"high-byte-add-jnc":"word-cmp-jc",r->outerLimit,r->argBytes,r->nativeDual?4:2,r->nativeDual?"[4,12]":"[4]",r->returned,targetHigh,targetLow,r->highSlot,r->lowSlot);
  dbuf_printf(&changed,"%s:\n\tpush dr24\n\tpush ar%u\n\tmov r%u,#0\n%s:\n\tmov a,@spx\n\tadd a,r%u\n\tmov r%u,a\n",entry,r->indexReg,r->sampleReg,outer,r->sampleReg,r->indexReg);
  for(unsigned i=r->control;i<r->inner-1;++i)dbuf_printf(&changed,"\t%s\n",l[i].node->line);
  dbuf_printf(&changed,"%s:\n",inner);
  for(unsigned i=r->inner;i<r->collect;++i) {
    if(i==r->store || i==r->store+1)continue;
    if(i==r->call)dbuf_printf(&changed,"\tmov wr24,@spx-0x%04x\n\tmov wr26,@spx-0x%04x\n\tecall @dr24\n",targetHigh,targetLow);
    else if(m251OuterBranch(l[i].text,r->innerHighByte?"jnc":"jc",r->innerLabel))dbuf_printf(&changed,"\t%s %s\n",r->innerHighByte?"jnc":"jc",inner);
    else dbuf_printf(&changed,"\t%s\n",l[i].node->line);
  }
  /* DEC 保留 OV/AC，却改变 N/Z。重放原最终比较恢复 CY/N/Z；
   * 两条路径都直接进入同一 ERET，不假定函数边界上 FLAGS 已死亡。 */
  dbuf_printf(&changed,"\tecall %s\n\tcjne r%u,#%u,%s\n%s:\n\tjnc %s\n\tejmp %s\n%s:\n\tdec spx,#4\n\tdec spx\n\tcjne r%u,#%u,%s\n%s:\n\teret\n",r->collector,r->sampleReg,r->outerLimit,check,check,done,outer,done,r->sampleReg,r->outerLimit,returnLabel,returnLabel);
  bool finalStores[M251_OUTER_MAX_COPIES];
  for(unsigned c=0;c<copies;++c)
    finalStores[c]=!(c+1<copies && m251OuterFinalStoreDead(l,count,regions+c,regions+c+1,old));
  for(unsigned c=0;c<copies;++c) {
    m251_outer_region *x=regions+c;unsigned slot=0;
    bool finalStore=finalStores[c];
    for(unsigned i=x->begin;i<=x->end;++i) {
      char text[M251_OUTLINE_TEXT];
      if(slot==0)snprintf(text,sizeof text,"mov wr26,#%s",x->target);
      else if(slot==1)snprintf(text,sizeof text,"movh dr24,#(%s >> 16)",x->target);
      else if(slot==2)snprintf(text,sizeof text,"mov r%u,#%u",x->indexReg,x->base);
      else if(slot==3)snprintf(text,sizeof text,"ecall %s",entry);
      else if(slot==4 && finalStore)snprintf(text,sizeof text,"%s",l[x->store].node->line);
      else if(slot==5 && finalStore)snprintf(text,sizeof text,"%s",l[x->store+1].node->line);
      else snprintf(text,sizeof text,"; MX251 outer-shared instruction");
      lineNode *node=l[i].node;node->line=Safe_strdup(text);node->isComment=slot>5 || (slot>=4 && !finalStore);node->isLabel=FALSE;node->aln=NULL;++slot;
    }
    dbuf_printf(&changed,"; MX251 outer-site {\"entry\":\"%s\",\"target\":\"%s\",\"base\":%u,\"sourceOuter\":\"%s\",\"sourceInner\":\"%s\",\"sourceExit\":\"%s\",\"objectKey\":%d,\"finalStore\":%s,\"nextSourceOuter\":\"%s\"}\n",entry,x->target,x->base,x->outerLabel,x->innerLabel,x->exitLabel,x->object->key,finalStore?"true":"false",finalStore?"":regions[c+1].outerLabel);
  }
  dbuf_set_length(helpers,0);dbuf_append(helpers,dbuf_get_buf(&changed),dbuf_get_length(&changed));dbuf_destroy(&changed);
  if(trace)fprintf(stderr,"outer-region accepted function=%s copies=%u helper=%s\n",currFunc->name,copies,entry);
  Safe_free(regions);Safe_free(l);return;
reject:
  if(trace)fprintf(stderr,"outer-region rejected function=%s reason=%s copies=%u\n",currFunc->name,why,copies);
  Safe_free(regions);Safe_free(l);
}
