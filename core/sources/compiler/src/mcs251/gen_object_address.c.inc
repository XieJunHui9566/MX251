/* GPL-2.0-or-later. 完整对象来源的局部原生地址发射；默认关闭。
 * 证明在分配之后执行，禁止改变对象布局、保存对或调用 ABI。
 * 未知 IR、别名、入口、活跃性和机器成本均使整个候选回退。 */
#define M251_OBJECT_MAX_LINES 8192
#define M251_OBJECT_MAX_POINTERS 128
#define M251_OBJECT_MAX_REGIONS 64
typedef struct { lineNode *node; char text[M251_OUTLINE_TEXT]; } m251_object_line;
typedef struct {
  unsigned begin,end,loop,get,call,tail;
  unsigned counter,limit,saved,index,changedMask;
  int objectOffset,pointerOffset;
  symbol *object;
  iCode *addressIC,*getIC,*sendIC,*callIC;
  char loopLabel[64],testLabel[64],target[256];
} m251_object_region;
typedef struct {
  unsigned begin,firstRead,secondAddress,secondRead,end,index,delta,valueSize;
  unsigned changedMask;
  int pointerOffset,firstDestination,secondDestination;
  symbol *object;
  iCode *firstIC,*castIC,*secondIC,*firstGet,*secondGet;
  operand *indexOperand;
} m251_object_code;
typedef struct { symbol *function; bool ordinaryLeaf; } m251_object_body;
static m251_object_body m251ObjectBodies[256];
static unsigned m251ObjectBodyCount;

static bool m251ObjectEqual(operand *a,operand *b)
{return a && b && isOperandEqual(a,b);}
static iCode *m251ObjectDefinition(operand *p)
{
  if(!p || !IS_ITEMP(p) || OP_SYMBOL(p)->remat || bitVectnBitsOn(OP_DEFS(p))!=1)return NULL;
  iCode *ic=hTabItemWithKey(iCodehTab,bitVectFirstBit(OP_DEFS(p)));
  return ic && m251ObjectEqual(IC_RESULT(ic),p)?ic:NULL;
}
static bool m251ObjectCodePointer(operand *p)
{
  if(!p || !IS_ITEMP(p) || isOperandVolatileOrAtomic(p,TRUE))return FALSE;
  sym_link *t=operandType(p);
  return IS_PTR(t) && DCL_TYPE(t)==CPOINTER && getSize(t)==3 &&
    !isVolatile(t->next) && !isAtomic(t->next);
}
static bool m251ObjectFlatPointer(operand *p)
{
  if(!p || !IS_SYMOP(p) || isOperandVolatileOrAtomic(p,TRUE))return FALSE;
  sym_link *t=operandType(p);
  return IS_PTR(t) && getSize(t)==3 &&
    (DCL_TYPE(t)==FPOINTER || DCL_TYPE(t)==GPOINTER) &&
    !isVolatile(t->next) && !isAtomic(t->next);
}
static bool m251ObjectCoreObserver(operand *p)
{
  if(!p || !IS_TRUE_SYMOP(p))return FALSE;
  sym_link *s=OP_SYM_ETYPE(p);
  if(!SPEC_ABSA(s))return FALSE;
  /* 任意绝对 RAM 对象可能观察寄存器银行或当前栈，不能用声明名排除别名。 */
  if(SPEC_SCLS(s)!=S_SFR && SPEC_SCLS(s)!=S_SBIT)return TRUE;
  unsigned a=SPEC_ADDR(s);if(SPEC_SCLS(s)==S_SBIT)a&=0xf8;
  /* 与目标无关的架构状态；包含已有 SPH 别名约束。 */
  return (a>=0x81 && a<=0x86) || a==0xbe || a==0xd0 || a==0xd1 ||
    a==0xe0 || a==0xf0;
}
static bool m251ObjectFunction(iCode *lic)
{
  if(!currFunc || !lic || options.useXstack || FUNC_SDCCCALL(currFunc->type)!=2 ||
     IFFUNC_ISISR(currFunc->type) || IFFUNC_ISNAKED(currFunc->type) ||
     IFFUNC_ISCRITICAL(currFunc->type) || IFFUNC_CALLEESAVES(currFunc->type) ||
     FUNC_REGBANK(currFunc->type))return FALSE;
  unsigned n=0;
  for(iCode *p=lic;p;p=p->next) {
    if(++n>M251_OBJECT_MAX_LINES || p->op==INLINEASM || p->op==JUMPTABLE ||
       p->op==CRITICAL || p->op==ENDCRITICAL)return FALSE;
    if(m251ObjectCoreObserver(IC_LEFT(p)) || m251ObjectCoreObserver(IC_RIGHT(p)) ||
       m251ObjectCoreObserver(IC_RESULT(p)))return FALSE;
    if(p->op==CAST && IC_RESULT(p) && IS_PTR(operandType(IC_RESULT(p))) &&
       (!IC_RIGHT(p) || !IS_PTR(operandType(IC_RIGHT(p)))))return FALSE;
  }
  return TRUE;
}
/* 字节接收者只接受当前 TU 已生成的普通 C leaf。该记录不改变 callee，
 * 也不摘要时间；只排除未知汇编/间接调用和 SP/PSW 显式观察。
 * 未见定义、后定义、递归/调用或容量耗尽均不予资格。最终机器体仍须绑定。 */
static void m251ObjectRememberBody(iCode *lic)
{
  if(!currFunc || m251ObjectBodyCount==256)return;
  bool safe=m251ObjectFunction(lic);
  for(iCode *p=lic;p;p=p->next)
    if(p->op==CALL || p->op==PCALL || p->op==IPUSH || p->op==IPOP ||
       POINTER_GET(p) || POINTER_SET(p) || p->op==ADDRESS_OF)safe=FALSE;
  m251ObjectBodies[m251ObjectBodyCount++]=(m251_object_body){currFunc,safe};
}
static bool m251ObjectSink(lineNode *line,char *name)
{
  iCode *ic=line->ic;
  if(!ic || ic->op!=CALL || !IS_SYMOP(IC_LEFT(ic)) || ic->parmBytes)return FALSE;
  symbol *s=OP_SYMBOL(IC_LEFT(ic));sym_link *t=s->type;
  if(!IS_FUNC(t) || FUNC_SDCCCALL(t)!=2 || !IFFUNC_ISREENT(t) ||
     IFFUNC_ISNAKED(t) || IFFUNC_CALLEESAVES(t) || IFFUNC_ISISR(t) ||
     IFFUNC_ISCRITICAL(t) || IFFUNC_HASVARARGS(t) || IFFUNC_ISBANKEDCALL(t) ||
     FUNC_REGBANK(t) || SPEC_NORETURN(getSpec(t)) || !IS_VOID(t->next))return FALSE;
  value *arg=FUNC_ARGS(t);
  if(!arg || arg->next || !IS_INTEGRAL(arg->type) || getSize(arg->type)!=1 ||
     !SPEC_USIGN(getSpec(arg->type)) || isVolatile(arg->type) || isAtomic(arg->type))return FALSE;
  bool found=FALSE;
  for(unsigned i=0;i<m251ObjectBodyCount;++i)
    if(m251ObjectBodies[i].function==s && m251ObjectBodies[i].ordinaryLeaf)found=TRUE;
  if(!found)return FALSE;
  char expected[M251_OUTLINE_TEXT],actual[M251_OUTLINE_TEXT];
  snprintf(expected,sizeof expected,"ecall%s",s->rname);m251OuterNormalize(line->line,actual);
  if(strcmp(expected,actual) || strlen(s->rname)>=256)return FALSE;
  strcpy(name,s->rname);return TRUE;
}
static bool m251ObjectFrame(symbol *s)
{
  return s && s->onStack && s->level && s->localof==currFunc && !s->_isparm &&
    SPEC_OCLS(s->etype) && !SPEC_OCLS(s->etype)->paged &&
    !SPEC_ABSA(s->etype) && !IS_STATIC(s->type) && IS_INTEGRAL(s->type) &&
    !IS_BITVAR(s->type) && !IS_BITFIELD(s->etype) &&
    getSize(s->type)>0 && getSize(s->type)<=255 &&
    !isVolatile(s->type) && !isAtomic(s->type);
}
static bool m251ObjectMember(operand **p,unsigned count,operand *op)
{for(unsigned i=0;i<count;++i)if(m251ObjectEqual(p[i],op))return TRUE;return FALSE;}
static bool m251ObjectAddPointer(operand **p,unsigned *count,operand *op)
{
  if(!m251ObjectFlatPointer(op))return FALSE;
  if(IS_TRUE_SYMOP(op)) {
    symbol *s=OP_SYMBOL(op);
    if(!s->onStack || !s->level || s->localof!=currFunc || s->addrtaken ||
       SPEC_ABSA(s->etype) || IS_STATIC(s->type))return FALSE;
  }
  if(m251ObjectMember(p,*count,op))return TRUE;
  if(*count==M251_OBJECT_MAX_POINTERS)return FALSE;p[(*count)++]=op;return TRUE;
}
/* 所有取址根及派生指针一起闭合。任何传参、返回、指针存储、比较、
 * 整数往返或来源不明的重定义都拒绝，不能仅以当前 GET 的类型推断无逃逸。
 * 接受的每一个 byte GET 还必须属于已经独立匹配的有限区域。 */
static bool m251ObjectUnescaped(iCode *lic,symbol *object,
                                m251_object_region *regions,unsigned regionCount)
{
  operand *pointers[M251_OBJECT_MAX_POINTERS];unsigned count=0;
  if(!m251ObjectFrame(object))return FALSE;
  for(iCode *p=lic;p;p=p->next)if(p->op==ADDRESS_OF && IS_TRUE_SYMOP(IC_LEFT(p)) &&
      OP_SYMBOL(IC_LEFT(p))==object) {
    if((IC_RIGHT(p) && (!IS_OP_LITERAL(IC_RIGHT(p)) || operandLitValue(IC_RIGHT(p))!=0)) ||
       !m251ObjectAddPointer(pointers,&count,IC_RESULT(p)))return FALSE;
  }
  if(!count)return FALSE;
  bool changed;
  do {
    changed=FALSE;unsigned previous=count;
    for(iCode *p=lic;p;p=p->next) {
      bool left=m251ObjectMember(pointers,count,IC_LEFT(p));
      bool right=m251ObjectMember(pointers,count,IC_RIGHT(p));
      if(((p->op=='=' && !POINTER_SET(p)) || p->op==CAST) && right) {
        if(!m251ObjectAddPointer(pointers,&count,IC_RESULT(p)))return FALSE;
      } else if(p->op=='+' && left) {
        operand *index=IC_RIGHT(p);
        if(!index || !IS_INTEGRAL(operandType(index)) ||
           isOperandVolatileOrAtomic(index,TRUE) ||
           !m251ObjectAddPointer(pointers,&count,IC_RESULT(p)))return FALSE;
      }
    }
    changed=previous!=count;
  } while(changed);
  for(iCode *p=lic;p;p=p->next) {
    bool left=m251ObjectMember(pointers,count,IC_LEFT(p));
    bool right=m251ObjectMember(pointers,count,IC_RIGHT(p));
    bool result=m251ObjectMember(pointers,count,IC_RESULT(p));
    if(!left && !right && !result)continue;
    bool root=p->op==ADDRESS_OF && IS_TRUE_SYMOP(IC_LEFT(p)) &&
      OP_SYMBOL(IC_LEFT(p))==object && result;
    bool copy=((p->op=='=' && !POINTER_SET(p)) || p->op==CAST) &&
      right && result && !left;
    bool add=p->op=='+' && left && result && !right;
    bool get=POINTER_GET(p) && left && !right && !result;
    if(root || copy || add)continue;
    if(!get || getSize(operandType(IC_RESULT(p)))!=1 ||
       isOperandVolatileOrAtomic(IC_RESULT(p),TRUE))return FALSE;
    bool found=FALSE;
    for(unsigned r=0;r<regionCount;++r)if(regions[r].object==object && regions[r].getIC==p)found=TRUE;
    if(!found)return FALSE;
  }
  return TRUE;
}
/* 标签仍原位保留；只有一个显式回边可以进入循环头，条件中继只能由
 * 相邻 CJNE 引用。对其它引用不猜测“不会执行”，整个候选直接回退。 */
static bool m251ObjectEntries(m251_object_line *l,unsigned count,const m251_object_region *r)
{
  unsigned loopRefs=0,testRefs=0;
  for(unsigned i=0;i<count;++i) {
    if(l[i].node->isLabel)continue;
    const char *comma=strrchr(l[i].text,',');
    const char *last=comma?comma+1:NULL;
    bool loop=(last && !strcmp(last,r->loopLabel)) ||
      m251OuterBranch(l[i].text,"jc",r->loopLabel) ||
      m251OuterBranch(l[i].text,"sjmp",r->loopLabel) ||
      m251OuterBranch(l[i].text,"ejmp",r->loopLabel);
    bool test=(last && !strcmp(last,r->testLabel)) ||
      m251OuterBranch(l[i].text,"jc",r->testLabel) ||
      m251OuterBranch(l[i].text,"sjmp",r->testLabel) ||
      m251OuterBranch(l[i].text,"ejmp",r->testLabel);
    /* 文本中的额外符号引用也拒绝，例如未知跳转或取址。 */
    if(strstr(l[i].text,r->loopLabel) && !loop)return FALSE;
    if(strstr(l[i].text,r->testLabel) && !test)return FALSE;
    if(loop){if(i!=r->end || ++loopRefs!=1)return FALSE;}
    if(test){if(i!=r->tail+1 || ++testRefs!=1)return FALSE;}
  }
  return loopRefs==1 && testRefs==1;
}
static bool m251ObjectByteSlot(const char *text,bool store,int *offset)
{
  char slot[64],*end;int n=0;
  if((store?sscanf(text,"mov%63[^,],a%n",slot,&n):
            sscanf(text,"mova,%63s%n",slot,&n))!=1 || n<=0 || text[n] ||
     strncmp(slot,"@spx-",5))return FALSE;
  long off=strtol(slot+4,&end,0);
  if(*end || off < -32768 || off>=0)return FALSE;*offset=(int)off;return TRUE;
}
static bool m251ObjectLabel(const m251_object_line *line,char *label)
{
  size_t length=strlen(line->text);
  if(!line->node->isLabel || length<2 || length>=64 || line->text[length-1]!=':')return FALSE;
  memcpy(label,line->text,length-1);label[length-1]=0;return TRUE;
}
static bool m251ObjectStackParse(m251_object_line *l,unsigned count,unsigned begin,
                                 iCode *lic,m251_object_region *r,const char **why)
{
  unsigned k=begin,lo,mid,hi,a,b,zero,offset;int slot;
  char expected[M251_OUTLINE_TEXT];memset(r,0,sizeof *r);r->begin=begin;
  if(k+49>=count || strcmp(l[k++].text,"movdpx,spx") ||
     !m251OuterImm(l[k++].text,"subdpx,#",&offset) || offset>32765)return FALSE;
  r->objectOffset=-(int)offset;r->addressIC=l[begin].node->ic;
#define M251_OBJ_EXPECT(s) do{if(k>=count || strcmp(l[k++].text,(s))){*why="stack-shape";return FALSE;}}while(0)
#define M251_OBJ_FMT(...) do{snprintf(expected,sizeof expected,__VA_ARGS__);M251_OBJ_EXPECT(expected);}while(0)
  M251_OBJ_EXPECT("pushdpxl");M251_OBJ_EXPECT("pushdph");M251_OBJ_EXPECT("pushdpl");
  M251_OBJ_EXPECT("popacc");
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&lo) || lo>7)return FALSE;
  M251_OBJ_EXPECT("popacc");
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&mid) || mid>7)return FALSE;
  M251_OBJ_EXPECT("popacc");
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&hi) || hi>7 ||
     lo==mid || lo==hi || mid==hi)return FALSE;
  M251_OBJ_FMT("mova,r%u",lo);
  if(!m251ObjectByteSlot(l[k++].text,TRUE,&r->pointerOffset))return FALSE;
  M251_OBJ_FMT("mova,r%u",mid);
  if(!m251ObjectByteSlot(l[k++].text,TRUE,&slot) || slot!=r->pointerOffset-1)return FALSE;
  M251_OBJ_FMT("mova,r%u",hi);
  if(!m251ObjectByteSlot(l[k++].text,TRUE,&slot) || slot!=r->pointerOffset-2)return FALSE;
  if(!m251OuterMatch2(l[k++].text,"movr%u,#%i%n",&r->counter,&zero) || zero || r->counter>7 ||
     r->counter==lo || r->counter==mid || r->counter==hi ||
     !m251ObjectLabel(l+k,r->loopLabel))return FALSE;
  r->loop=k++;
  if(!m251OuterMatch1(l[k++].text,"pushar%u%n",&a) ||
     !m251OuterMatch1(l[k++].text,"pushar%u%n",&b) || a!=b+1 || (b&1) || b>6 ||
     r->counter==a || r->counter==b)return FALSE;
  r->saved=b;
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset-2)return FALSE;
  M251_OBJ_FMT("adda,r%u",r->counter);M251_OBJ_FMT("movr%u,a",a);
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset-3)return FALSE;
  M251_OBJ_EXPECT("addca,#0x00");M251_OBJ_FMT("movr%u,a",b);
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset-4)return FALSE;
  M251_OBJ_EXPECT("addca,#0x00");
  unsigned bank;
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&bank) || bank>7 || bank==a || bank==b ||
     bank==r->counter)return FALSE;
  r->changedMask=(1u<<lo)|(1u<<mid)|(1u<<hi)|(1u<<bank);
  M251_OBJ_FMT("movdpl,r%u",a);M251_OBJ_FMT("movdph,r%u",b);M251_OBJ_FMT("movdpxl,r%u",bank);
  M251_OBJ_EXPECT("movdr28,dpx");r->get=k;
  M251_OBJ_EXPECT("ecall__gptrget");r->getIC=l[r->get].node->ic;
  M251_OBJ_FMT("movr%u,a",a);M251_OBJ_EXPECT("movdpl,a");
  M251_OBJ_FMT("pushwr%u",b);M251_OBJ_FMT("pushar%u",r->counter);
  r->call=k;r->callIC=l[k].node->ic;
  if(!m251ObjectSink(l[k++].node,r->target)){*why="byte-sink-not-proven-local-leaf";return FALSE;}
  M251_OBJ_FMT("popar%u",r->counter);M251_OBJ_FMT("popwr%u",b);
  r->tail=k;M251_OBJ_FMT("incr%u",r->counter);
  int used=0,limit=0;
  if(sscanf(l[k++].text,"cjner%u,#%i,%63[^:]%n",&a,&limit,r->testLabel,&used)!=3 ||
     used<=0 || l[k-1].text[used] || a!=r->counter || limit<=0 || limit>255)return FALSE;
  r->limit=(unsigned)limit;M251_OBJ_FMT("%s:",r->testLabel);
  M251_OBJ_FMT("popar%u",b);M251_OBJ_FMT("popar%u",b+1);M251_OBJ_FMT("jc%s",r->loopLabel);
  r->end=k-1;
#undef M251_OBJ_FMT
#undef M251_OBJ_EXPECT
  if(!r->addressIC || r->addressIC->op!=ADDRESS_OF || !IS_TRUE_SYMOP(IC_LEFT(r->addressIC)) ||
     !r->getIC || !POINTER_GET(r->getIC) ||
     !m251ObjectFlatPointer(IC_LEFT(r->getIC)) ||
     (IC_RIGHT(r->getIC) && (!IS_OP_LITERAL(IC_RIGHT(r->getIC)) || operandLitValue(IC_RIGHT(r->getIC))!=0)) ||
     !IC_RESULT(r->getIC) || !IS_INTEGRAL(operandType(IC_RESULT(r->getIC))) ||
     getSize(operandType(IC_RESULT(r->getIC)))!=1 ||
     isOperandVolatileOrAtomic(IC_RESULT(r->getIC),TRUE)) {*why="object-source-or-byte-read";return FALSE;}
  r->object=OP_SYMBOL(IC_LEFT(r->addressIC));
  if(!m251ObjectFrame(r->object) || r->limit>(unsigned)getSize(r->object->type) ||
     (IC_RIGHT(r->addressIC) && (!IS_OP_LITERAL(IC_RIGHT(r->addressIC)) || operandLitValue(IC_RIGHT(r->addressIC))!=0))) {
    *why="complete-frame-object-bounds";return FALSE;
  }
  iCode *add=m251ObjectDefinition(IC_LEFT(r->getIC));
  iCode *cast=add && add->op=='+'?m251ObjectDefinition(IC_LEFT(add)):NULL;
  operand *counter=add?IC_RIGHT(add):NULL;
  if(!add || !cast || cast->op!=CAST ||
     !m251ObjectEqual(IC_RIGHT(cast),IC_RESULT(r->addressIC)) ||
     !counter || !IS_ITEMP(counter) || !IS_INTEGRAL(operandType(counter)) ||
     getSize(operandType(counter))!=1 || !SPEC_USIGN(getSpec(operandType(counter))) ||
     isOperandVolatileOrAtomic(counter,TRUE) || OP_SYMBOL(counter)->isspilt ||
     OP_SYMBOL(counter)->remat || OP_SYMBOL(counter)->nRegs!=1 ||
     !OP_SYMBOL(counter)->regs[0] || OP_SYMBOL(counter)->regs[0]->rIdx!=mcs251_regIdxForNumber(r->counter) ||
     add->next!=r->getIC || r->addressIC->next!=cast) {
    *why="frame-address-cast-index-binding";return FALSE;
  }
  struct valinfo indexRange=getOperandValinfo(add,counter);
  symbol *pointerSlot=OP_SYMBOL(IC_RESULT(cast))->usl.spillLoc;
  if(indexRange.nothing || indexRange.anything || indexRange.min<0 || indexRange.max>=r->limit ||
     !OP_SYMBOL(IC_RESULT(cast))->isspilt || !pointerSlot || !pointerSlot->onStack ||
     getSize(pointerSlot->type)!=3 ||
     r->objectOffset-(r->pointerOffset-2)!=r->object->stack-pointerSlot->stack) {
    *why="frame-index-bounds-or-stack-layout";return FALSE;
  }
  for(unsigned i=begin;i<begin+17;++i)
    if(l[i].node->ic!=(i<begin+11?r->addressIC:cast)) {
      *why="frame-address-ic-binding";return FALSE;
    }
  for(unsigned i=r->loop+3;i<r->get-4;++i)
    if(l[i].node->ic!=add){*why="frame-add-ic-binding";return FALSE;}
  for(unsigned i=r->get-4;i<=r->get+1;++i)
    if(l[i].node->ic!=r->getIC){*why="frame-get-ic-binding";return FALSE;}
  /* 唯一 byte GET -> SEND -> 已绑定直接 CALL。只有这一条 MOV DPL,A
   * 允许来自已知 peephole 的空 IC；不得将任意丢失来源的指令放进区域。 */
  operand *byte=IC_RESULT(r->getIC);
  if(!IS_ITEMP(byte) || bitVectnBitsOn(OP_USES(byte))!=1) {
    *why="byte-send-use-closure";return FALSE;
  }
  r->sendIC=hTabItemWithKey(iCodehTab,bitVectFirstBit(OP_USES(byte)));
  if(!r->sendIC || r->sendIC->op!=SEND || r->sendIC->argreg!=1 ||
     !m251ObjectEqual(IC_LEFT(r->sendIC),byte) || r->getIC->next!=r->sendIC ||
     r->sendIC->next!=r->callIC) {*why="byte-send-call-adjacency";return FALSE;}
  for(unsigned i=begin;i<k;++i) {
    lineNode *p=l[i].node;
    if(i==r->get+2 && !p->ic && !p->isInline && !p->isLabel &&
       !strcmp(l[i].text,"movdpl,a"))continue;
    if(p->isInline || !m251StoreHasIC(lic,p->ic) ||
       (p->isLabel && i!=r->loop && i!=r->tail+2) ||
       (!p->isLabel && !m251CallRegionIR(p))) {*why="ir-or-observable-boundary";return FALSE;}
  }
  if(!m251ObjectEntries(l,count,r)){*why="extra-loop-entry";return FALSE;}
  return TRUE;
}
static bool m251ObjectCodeRead(m251_object_line *l,unsigned count,unsigned *at,
                               unsigned size,iCode *get,int *destination)
{
  if(!get || !POINTER_GET(get) || !m251ObjectCodePointer(IC_LEFT(get)) ||
     (IC_RIGHT(get) && (!IS_OP_LITERAL(IC_RIGHT(get)) || operandLitValue(IC_RIGHT(get))!=0)) ||
     !IC_RESULT(get) || !IS_INTEGRAL(operandType(IC_RESULT(get))) ||
     getSize(operandType(IC_RESULT(get)))!=size || isOperandVolatileOrAtomic(IC_RESULT(get),TRUE))return FALSE;
  for(unsigned i=0;i<size;++i) {
    int off;unsigned k=*at;
    if(k+3>=count || strcmp(l[k++].text,"clra") || strcmp(l[k++].text,"movca,@a+dptr") ||
       !m251ObjectByteSlot(l[k++].text,TRUE,&off))return FALSE;
    if(!i)*destination=off;else if(off!=*destination+(int)i)return FALSE;
    if(i+1<size && strcmp(l[k++].text,"incdpx"))return FALSE;
    for(unsigned j=*at;j<k;++j)if(l[j].node->ic!=get)return FALSE;
    *at=k;
  }
  return TRUE;
}
static bool m251ObjectCodeParse(m251_object_line *l,unsigned count,unsigned begin,
                                iCode *lic,m251_object_code *r,const char **why)
{
  unsigned k=begin,low,pair,a,b,c,delta;int slot;char expected[M251_OUTLINE_TEXT];
  memset(r,0,sizeof *r);r->begin=begin;r->valueSize=4;
  if(begin+57>=count || !m251OuterMatch1(l[k++].text,"mova,r%u%n",&low) ||
     low>15 || !(low&1) || low==11)return FALSE;
  r->index=low-1;r->firstIC=l[begin].node->ic;
  if(!r->firstIC || r->firstIC->op!='+' || !IS_ITEMP(IC_RESULT(r->firstIC)))return FALSE;
  operand *base=IC_LEFT(r->firstIC),*index=IC_RIGHT(r->firstIC);
  if(base && !IS_PTR(operandType(base))){operand *swap=base;base=index;index=swap;}
  r->object=mcs251CodeWordObject(base,r->valueSize,0);r->indexOperand=index;
  if(!r->object || !index || !IS_ITEMP(index) || getSize(operandType(index))!=2 ||
     !IS_INTEGRAL(operandType(index)) || !SPEC_USIGN(getSpec(operandType(index))) ||
     isOperandVolatileOrAtomic(index,TRUE))return FALSE;
  symbol *indexSymbol=OP_SYMBOL(index);
  if(indexSymbol->isspilt || indexSymbol->remat || indexSymbol->nRegs!=2 ||
     !indexSymbol->regs[0] || !indexSymbol->regs[1] ||
     indexSymbol->regs[0]->rIdx!=mcs251_regIdxForNumber(low) ||
     indexSymbol->regs[1]->rIdx!=mcs251_regIdxForNumber(r->index)) {
    *why="code-index-physical-bytepair";return FALSE;
  }
  const char *object=r->object->rname;
#define M251_CODE_EXPECT(s) do{if(k>=count || strcmp(l[k++].text,(s))){*why="code-shape";return FALSE;}}while(0)
#define M251_CODE_FMT(...) do{snprintf(expected,sizeof expected,__VA_ARGS__);M251_CODE_EXPECT(expected);}while(0)
  M251_CODE_FMT("adda,#%s",object);
  if(!m251ObjectByteSlot(l[k++].text,TRUE,&r->pointerOffset))return FALSE;
  M251_CODE_FMT("mova,r%u",r->index);M251_CODE_FMT("addca,#(%s>>8)",object);
  if(!m251ObjectByteSlot(l[k++].text,TRUE,&slot) || slot!=r->pointerOffset-1)return FALSE;
  M251_CODE_EXPECT("clra");M251_CODE_FMT("addca,#(%s>>16)",object);
  if(!m251ObjectByteSlot(l[k++].text,TRUE,&slot) || slot!=r->pointerOffset-2)return FALSE;
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset)return FALSE;
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&a) || a>15)return FALSE;
  if(!m251OuterSlot(l[k++].text,FALSE,&slot,&pair) || slot!=r->pointerOffset-2 || pair>14 || pair==10 || (pair&1))return FALSE;
  M251_CODE_FMT("mova,r%u",pair);M251_CODE_FMT("movdpl,r%u",a);
  M251_CODE_FMT("movdph,r%u",pair+1);M251_CODE_FMT("movdpxl,r%u",pair);
  r->changedMask=(1u<<a)|(3u<<pair);r->firstRead=k;r->firstGet=l[k].node->ic;
  if(!m251ObjectCodeRead(l,count,&k,r->valueSize,r->firstGet,&r->firstDestination))return FALSE;
  r->secondAddress=k;r->secondIC=l[k].node->ic;
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset ||
     !m251OuterImm(l[k++].text,"adda,#",&delta) || !delta || delta>255)return FALSE;
  r->delta=delta;
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&a) || a>15)return FALSE;
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset-1)return FALSE;
  M251_CODE_EXPECT("addca,#0x00");
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&b) || b>15)return FALSE;
  if(!m251ObjectByteSlot(l[k++].text,FALSE,&slot) || slot!=r->pointerOffset-2)return FALSE;
  M251_CODE_EXPECT("addca,#0x00");
  if(!m251OuterMatch1(l[k++].text,"movr%u,a%n",&c) || c>15)return FALSE;
  M251_CODE_FMT("movdpl,r%u",a);M251_CODE_FMT("movdph,r%u",b);M251_CODE_FMT("movdpxl,r%u",c);
  r->changedMask|=(1u<<a)|(1u<<b)|(1u<<c);r->secondRead=k;r->secondGet=l[k].node->ic;
  if(!m251ObjectCodeRead(l,count,&k,r->valueSize,r->secondGet,&r->secondDestination))return FALSE;r->end=k-1;
#undef M251_CODE_FMT
#undef M251_CODE_EXPECT
  operand *pointer=IC_RESULT(r->firstIC);
  r->castIC=m251ObjectDefinition(IC_LEFT(r->firstGet));
  if(!m251ObjectCodePointer(pointer) || !r->castIC || r->castIC->op!=CAST ||
     !m251ObjectEqual(IC_RIGHT(r->castIC),pointer) ||
     bitVectnBitsOn(OP_USES(IC_RESULT(r->castIC)))!=1 ||
     !bitVectBitValue(OP_USES(IC_RESULT(r->castIC)),r->firstGet->key) ||
     !r->secondIC || r->secondIC->op!='+' || !m251ObjectEqual(IC_LEFT(r->secondIC),pointer) ||
     !IS_OP_LITERAL(IC_RIGHT(r->secondIC)) || operandLitValue(IC_RIGHT(r->secondIC))!=delta ||
     !IS_ITEMP(IC_RESULT(r->secondIC)) || !r->secondGet || !POINTER_GET(r->secondGet) ||
     !m251ObjectEqual(IC_LEFT(r->secondGet),IC_RESULT(r->secondIC)) ||
     bitVectnBitsOn(OP_DEFS(pointer))!=1 || !bitVectBitValue(OP_DEFS(pointer),r->firstIC->key) ||
     bitVectnBitsOn(OP_USES(pointer))!=2 || !bitVectBitValue(OP_USES(pointer),r->castIC->key) ||
     !bitVectBitValue(OP_USES(pointer),r->secondIC->key) ||
     bitVectnBitsOn(OP_DEFS(IC_RESULT(r->secondIC)))!=1 ||
     bitVectnBitsOn(OP_USES(IC_RESULT(r->secondIC)))!=1 ||
     !bitVectBitValue(OP_USES(IC_RESULT(r->secondIC)),r->secondGet->key)) {
    *why="shared-code-pointer-use-closure";return FALSE;
  }
  if(r->firstIC->next!=r->castIC || r->castIC->next!=r->firstGet ||
     r->firstGet->next!=r->secondIC || r->secondIC->next!=r->secondGet) {
    *why="code-pointer-definition-order";return FALSE;
  }
  /* 原指针临时值与两个完整结果的实际 SPX 槽必须独立。数值布局和
   * 原始 GET 的逐行归属一起检查，不能只相信声明大小或文本长度。 */
  for(unsigned part=0;part<2;++part) {
    int dest=part?r->secondDestination:r->firstDestination;
    operand *value=IC_RESULT(part?r->secondGet:r->firstGet);
    symbol *pointerSlot=OP_SYMBOL(pointer)->usl.spillLoc;
    symbol *valueSlot=IS_ITEMP(value)?OP_SYMBOL(value)->usl.spillLoc:NULL;
    if(!OP_SYMBOL(pointer)->isspilt || !pointerSlot || !pointerSlot->onStack ||
       !IS_ITEMP(value) || !OP_SYMBOL(value)->isspilt || !valueSlot || !valueSlot->onStack ||
       getSize(valueSlot->type)!=r->valueSize ||
       /* 三字节指针的最低内存字节是 bank，标量的最低内存字节是 LSB。 */
       dest-(r->pointerOffset-2)!=valueSlot->stack-pointerSlot->stack) {
      *why="code-result-allocated-stack-layout";return FALSE;
    }
    for(unsigned j=0;j<r->valueSize;++j)
      if(dest+(int)j<=r->pointerOffset && dest+(int)j>=r->pointerOffset-2) {
        *why="code-result-pointer-slot-overlap";return FALSE;
      }
  }
  struct valinfo range=getOperandValinfo(r->firstIC,index);
  if(range.nothing || range.anything || range.min<0 || range.max>65535 ||
     range.max+(long long)delta+r->valueSize>getSize(r->object->type)) {
    *why="code-index-bound-or-overflow";return FALSE;
  }
  /* 旧路径改写的普通寄存器只能在第二次读取后死亡。原索引在两个地址
   * 之间仅遇到已匹配的 SPX 写和 CODE 读，因此新路径可以重用同一值。 */
  if(!r->secondGet->rSurv || bitVectBitValue(r->secondGet->rSurv,CND_IDX)) {
    *why="unknown-live-mask-or-flags";return FALSE;
  }
  for(unsigned i=0;i<16;++i)if((r->changedMask&(1u<<i)) &&
      bitVectBitValue(r->secondGet->rSurv,mcs251_regIdxForNumber(i))) {
    *why="old-address-register-live";return FALSE;
  }
  for(unsigned i=begin;i<k;++i)
    if(l[i].node->isLabel || l[i].node->isInline || !m251StoreHasIC(lic,l[i].node->ic) ||
       !m251CallRegionIR(l[i].node)) {*why="code-ir-or-observable-boundary";return FALSE;}
  for(unsigned i=begin;i<r->firstRead;++i) {
    iCode *expected=i<begin+9?r->firstIC:(i<begin+13?r->castIC:r->firstGet);
    if(l[i].node->ic!=expected){*why="code-address-ic-binding";return FALSE;}
  }
  for(unsigned i=r->secondAddress;i<r->secondRead;++i)
    if(l[i].node->ic!=(i<r->secondAddress+9?r->secondIC:r->secondGet)) {
      *why="code-second-address-ic-binding";return FALSE;
    }
  return TRUE;
}

#include "wide_cost.h"
static wide_cost_operand m251ObjectCostOperand(const char *text,const void *context)
{
  (void)context;unsigned reg;
  if(!strcmp(text,"dpl") || !strcmp(text,"dph") || !strcmp(text,"dpxl") ||
     !strcmp(text,"acc") || (m251OuterMatch1(text,"ar%u%n",&reg) && reg<8))
    return (wide_cost_operand){WC_DIRECT,1,0};
  return (wide_cost_operand){WC_UNKNOWN,0,0};
}
static unsigned m251ObjectCost(const char *text)
{
  char normalized[M251_OUTLINE_TEXT];m251OuterNormalize(text,normalized);
  unsigned reg;int literal,used=0;char label[64];
  if(!strncmp(normalized,"addca,#",7) && normalized[7])return 2;
  if(m251OuterMatch1(normalized,"addca,r%u%n",&reg) && reg<8)return 2;
  if(sscanf(normalized,"cjner%u,#%i,%63s%n",&reg,&literal,label,&used)==3 &&
     used>0 && !normalized[used] && reg<8 && literal>=0 && literal<=255)return 4;
  return wide_instruction_bytes_context(text,strlen(text),m251ObjectCostOperand,NULL);
}
static bool m251ObjectMeasure(m251_object_line *l,unsigned begin,unsigned end,
                               unsigned *bytes,unsigned *instructions)
{
  *bytes=*instructions=0;
  for(unsigned i=begin;i<=end;++i)if(!l[i].node->isLabel && !l[i].node->isComment) {
    unsigned n=m251ObjectCost(l[i].node->line);if(!n || *bytes>65535-n)return FALSE;
    *bytes+=n;++*instructions;
  }
  return TRUE;
}
static void m251ObjectReplace(lineNode *p,const char *text,iCode *ic,bool comment)
{
  p->line=Safe_strdup(text);p->ic=ic;p->isComment=comment;p->isLabel=FALSE;p->aln=NULL;
}
static void m251ObjectMeta(struct dbuf_s *out,const char *kind,symbol *object,
                           unsigned beforeBytes,unsigned afterBytes,
                           unsigned beforeInstructions,unsigned afterInstructions,
                           m251_object_line *l,unsigned begin,unsigned end,const char *proof)
{
  dbuf_printf(out,"; MX251 object-address {\"schema\":1,\"kind\":\"%s\",\"object\":\"%s\",\"objectSize\":%d,\"beforeBytes\":%u,\"afterBytes\":%u,\"beforeInstructions\":%u,\"afterInstructions\":%u,\"fixedAllocation\":true,\"requiresFinalObjectAndIsrClosure\":true,\"before\":[",
    kind,object->rname,getSize(object->type),beforeBytes,afterBytes,beforeInstructions,afterInstructions);
  for(unsigned i=begin;i<=end;++i){if(i!=begin)dbuf_append_char(out,',');m251StoreJsonString(out,l[i].node->line);}
  dbuf_printf(out,"],\"proof\":%s}",proof);
}
static bool m251ObjectStackApply(m251_object_line *l,const m251_object_region *r,lineNode *head,const char **why)
{
  unsigned beforeBytes,beforeInstructions;
  if(!m251ObjectMeasure(l,r->begin,r->end,&beforeBytes,&beforeInstructions)) {
    *why="unknown-stack-before-cost";return FALSE;
  }
  /* 删除的普通寄存器写不能被区域后继使用。按最终流倒序证明，避免第二个
   * 变换恰好删除第一个变换依赖的旧覆盖点。缺失/未知 CFG 会保守返回 false。 */
  for(unsigned i=0;i<8;++i)if(r->changedMask&(1u<<i)) {
    char reg[8];snprintf(reg,sizeof reg,"r%u",i);
    if(!mcs251notUsed(reg,l[r->end].node,head)){*why="frame-address-register-live";return FALSE;}
  }
  if(!r->getIC->rSurv || bitVectBitValue(r->getIC->rSurv,CND_IDX)) {
    *why="unknown-frame-live-mask-or-flags";return FALSE;
  }
  char replacement[12][M251_OUTLINE_TEXT];unsigned n=0,afterBytes=0;
#define M251_OBJ_NEW(...) do{snprintf(replacement[n++],M251_OUTLINE_TEXT,__VA_ARGS__);}while(0)
  M251_OBJ_NEW("mov\tdpx,spx");M251_OBJ_NEW("sub\tdpx,#%d",2-r->objectOffset);
  M251_OBJ_NEW("mov\tdr16,dpx");M251_OBJ_NEW("movz\twr22,r%u",r->counter);
  M251_OBJ_NEW("add\twr18,wr22");M251_OBJ_NEW("mov\ta,dpxl");
  M251_OBJ_NEW("addc\ta,#0");M251_OBJ_NEW("movz\twr16,a");
  M251_OBJ_NEW("mov\tdpx,dr16");M251_OBJ_NEW("mov\tdr28,dpx");
  M251_OBJ_NEW("mov\ta,@dpx");M251_OBJ_NEW("mov\tdpl,a");
#undef M251_OBJ_NEW
  unsigned addressBegin=r->loop+3,addressEnd=r->call-3,oldAddressBytes,oldAddressInstructions;
  unsigned oldPrefixBytes,oldPrefixInstructions;
  if(n>addressEnd-addressBegin+1 ||
     !m251ObjectMeasure(l,addressBegin,addressEnd,&oldAddressBytes,&oldAddressInstructions) ||
     !m251ObjectMeasure(l,r->begin,r->loop-2,&oldPrefixBytes,&oldPrefixInstructions))return FALSE;
  for(unsigned i=0;i<n;++i){unsigned bytes=m251ObjectCost(replacement[i]);if(!bytes){*why="unknown-stack-after-cost";return FALSE;}afterBytes+=bytes;}
  afterBytes=beforeBytes-oldPrefixBytes-oldAddressBytes+afterBytes;
  unsigned afterInstructions=beforeInstructions-oldPrefixInstructions-oldAddressInstructions+n;
  if(afterBytes>=beforeBytes || n>=oldAddressInstructions) {*why="stack-cost-not-improved";return FALSE;}
  struct dbuf_s meta;dbuf_init(&meta,4096);
  char proof[1024];snprintf(proof,sizeof proof,
    "{\"addressIC\":%d,\"getIC\":%d,\"sendIC\":%d,\"callIC\":%d,\"objectOffset\":%d,\"pointerLsbOffset\":%d,\"counterRegister\":%u,\"limit\":%u,\"sink\":\"%s\",\"changedMask\":%u,\"flags\":\"same-final-bank-ADDC-CY-AC-OV-N-Z; same-final-A-parity\",\"dr28Preserved\":true,\"callSavePairsPreserved\":true}",
    r->addressIC->key,r->getIC->key,r->sendIC->key,r->callIC->key,r->objectOffset,r->pointerOffset,r->counter,r->limit,r->target,r->changedMask);
  m251ObjectMeta(&meta,"frame-byte-view",r->object,beforeBytes,afterBytes,beforeInstructions,afterInstructions,l,r->begin,r->end,proof);
  for(unsigned i=r->begin;i<r->loop-1;++i)m251ObjectReplace(l[i].node,
    i==r->begin?dbuf_c_str(&meta):"; MX251 private pointer materialization removed",NULL,TRUE);
  for(unsigned i=addressBegin;i<=addressEnd;++i) {
    unsigned at=i-addressBegin;
    m251ObjectReplace(l[i].node,at<n?replacement[at]:"; MX251 known object generic read removed",at<n?r->getIC:NULL,at>=n);
  }
  dbuf_destroy(&meta);return TRUE;
}
static bool m251ObjectCodeApply(m251_object_line *l,const m251_object_code *r,lineNode *head,const char **why)
{
  unsigned beforeBytes,beforeInstructions,oldAddressBytes=0,oldAddressInstructions=0;
  if(!m251ObjectMeasure(l,r->begin,r->end,&beforeBytes,&beforeInstructions)) {*why="unknown-code-before-cost";return FALSE;}
  for(unsigned i=0;i<16;++i)if(r->changedMask&(1u<<i)) {
    char reg[8];snprintf(reg,sizeof reg,"r%u",i);
    if(!mcs251notUsed(reg,l[r->end].node,head)){*why="code-address-final-register-live";return FALSE;}
  }
  char text[2][6][M251_OUTLINE_TEXT];unsigned newBytes=0;
  for(unsigned part=0;part<2;++part) {
    char base[256];
    if(part)snprintf(base,sizeof base,"(%s+%u)",r->object->rname,r->delta);
    else snprintf(base,sizeof base,"%s",r->object->rname);
    snprintf(text[part][0],M251_OUTLINE_TEXT,"mov\twr18,wr%u",r->index);
    snprintf(text[part][1],M251_OUTLINE_TEXT,"add\twr18,#%s",base);
    snprintf(text[part][2],M251_OUTLINE_TEXT,"clr\ta");
    snprintf(text[part][3],M251_OUTLINE_TEXT,"addc\ta,#(%s>>16)",base);
    snprintf(text[part][4],M251_OUTLINE_TEXT,"movz\twr16,a");
    snprintf(text[part][5],M251_OUTLINE_TEXT,"mov\tdpx,dr16");
    unsigned bytes,instructions,begin=part?r->secondAddress:r->begin,end=(part?r->secondRead:r->firstRead)-1;
    if(!m251ObjectMeasure(l,begin,end,&bytes,&instructions) || end-begin+1<7)return FALSE;
    oldAddressBytes+=bytes;oldAddressInstructions+=instructions;
    for(unsigned i=0;i<6;++i){unsigned n=m251ObjectCost(text[part][i]);if(!n){*why="unknown-code-after-cost";return FALSE;}newBytes+=n;}
  }
  if(newBytes>=oldAddressBytes || oldAddressInstructions<=12) {*why="code-cost-not-improved";return FALSE;}
  struct dbuf_s meta;dbuf_init(&meta,4096);
  struct valinfo range=getOperandValinfo(r->firstIC,r->indexOperand);
  char proof[1024];snprintf(proof,sizeof proof,
    "{\"firstIC\":%d,\"castIC\":%d,\"firstGet\":%d,\"secondIC\":%d,\"secondGet\":%d,\"indexWordRegister\":%u,\"indexMin\":%lld,\"indexMax\":%lld,\"delta\":%u,\"pointerLsbOffset\":%d,\"firstDestination\":%d,\"secondDestination\":%d,\"changedMask\":%u,\"flags\":\"same-bank-final-ADDC-and-retained-INC; same-final-A-parity\",\"requiresCompleteCodeObjectInFFBank\":true}",
    r->firstIC->key,r->castIC->key,r->firstGet->key,r->secondIC->key,r->secondGet->key,r->index,(long long)range.min,(long long)range.max,r->delta,r->pointerOffset,r->firstDestination,r->secondDestination,r->changedMask);
  m251ObjectMeta(&meta,"code-shared-base",r->object,beforeBytes,beforeBytes-oldAddressBytes+newBytes,
    beforeInstructions,beforeInstructions-oldAddressInstructions+12,l,r->begin,r->end,proof);
  for(unsigned part=0;part<2;++part) {
    unsigned begin=part?r->secondAddress:r->begin,end=(part?r->secondRead:r->firstRead)-1;
    for(unsigned i=begin;i<=end;++i) {
      unsigned at=i-begin;
      if(!at)m251ObjectReplace(l[i].node,part?"; MX251 same complete CODE object base":dbuf_c_str(&meta),NULL,TRUE);
      else if(at<=6)m251ObjectReplace(l[i].node,text[part][at-1],part?r->secondIC:r->firstIC,FALSE);
      else m251ObjectReplace(l[i].node,"; MX251 closed shared pointer materialization removed",NULL,TRUE);
    }
  }
  dbuf_destroy(&meta);return TRUE;
}
static void mcs251ObjectNativeAddresses(lineNode *head,iCode *lic)
{
  const char *enabled=getenv("MX251_OBJECT_ADDRESS_NATIVE");
  if(!enabled || strcmp(enabled,"1"))return;
  const char *capability=getenv("MX251_OBJECT_ADDRESS_CAPABILITY");
  if(!capability || strcmp(capability,"mcs251-flat24-codeff-spx-v1"))return;
  m251ObjectRememberBody(lic);
  if(!m251ObjectFunction(lic))return;
  bool trace=getenv("MX251_OBJECT_ADDRESS_TRACE")!=NULL;
  unsigned count=0;for(lineNode *p=head;p;p=p->next){if(p->isInline)return;if(!p->isComment && !p->isDebug)++count;}
  if(!count || count>M251_OBJECT_MAX_LINES)return;
  m251_object_line *l=Safe_alloc(count*sizeof(*l));unsigned at=0;
  for(lineNode *p=head;p;p=p->next)if(!p->isComment && !p->isDebug){l[at].node=p;m251OuterNormalize(p->line,l[at++].text);}
  m251_object_region frames[M251_OBJECT_MAX_REGIONS];m251_object_code codes[M251_OBJECT_MAX_REGIONS];
  unsigned frameCount=0,codeCount=0,accepted=0;const char *why="no-complete-local-shape";
  for(unsigned i=0;i<count;++i) {
    if(frameCount<M251_OBJECT_MAX_REGIONS && m251ObjectStackParse(l,count,i,lic,frames+frameCount,&why)) {i=frames[frameCount++].end;continue;}
    if(codeCount<M251_OBJECT_MAX_REGIONS && m251ObjectCodeParse(l,count,i,lic,codes+codeCount,&why))i=codes[codeCount++].end;
  }
  /* 两种区域统一按机器流倒序提交，所有死亡覆盖检查观察最终后继。 */
  unsigned frameAt=frameCount,codeAt=codeCount;
  while(frameAt || codeAt) {
    if(frameAt && (!codeAt || frames[frameAt-1].begin>codes[codeAt-1].begin)) {
      m251_object_region *r=frames+--frameAt;
      if(!m251ObjectUnescaped(lic,r->object,frames,frameCount)){why="frame-pointer-escape-or-uncovered-use";continue;}
      if(m251ObjectStackApply(l,r,head,&why))++accepted;
    } else if(m251ObjectCodeApply(l,codes+--codeAt,head,&why))++accepted;
  }
  if(trace)fprintf(stderr,"object-address function=%s frame=%u code=%u accepted=%u last=%s\n",currFunc->name,frameCount,codeCount,accepted,why);
  Safe_free(l);
}
