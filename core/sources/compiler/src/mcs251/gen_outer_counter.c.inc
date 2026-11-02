/* SPDX-License-Identifier: GPL-2.0-or-later
 * 私有闭合镜像的有限计数器布局。契约必须来自当前真实发射代码的传递
 * 读写并集；这里只消费该事实，不以 ABI/函数类型代替寄存器效果证明。
 * 默认关闭；宿主还必须核对最终对象、入口 FLAGS、栈观察和 ISR 保存。 */
#define M251_COUNTER_MAX_TARGETS 512
typedef struct { char target[256]; unsigned long untouched; } m251_counter_target;
typedef struct {
  unsigned factor, count;
  char evidence[65];
  m251_counter_target targets[M251_COUNTER_MAX_TARGETS];
} m251_counter_contract;

static bool m251CounterHex(const char *s,unsigned n)
{
  if(strlen(s)!=n)return FALSE;
  for(unsigned i=0;i<n;++i)if(!isxdigit((unsigned char)s[i]))return FALSE;
  return TRUE;
}

static bool m251CounterRead(m251_counter_contract *c,const char **why)
{
  const char *enabled=getenv("MX251_OUTER_COUNTER"),*path=getenv("MX251_OUTER_COUNTER_CONTRACT");
  const char *factor=getenv("MX251_OUTER_COUNTER_UNROLL");
  memset(c,0,sizeof(*c));c->factor=1;
  if(!enabled || strcmp(enabled,"1")){*why="counter-disabled";return FALSE;}
  if(factor && *factor) {
    if(!strcmp(factor,"1"))c->factor=1;else if(!strcmp(factor,"2"))c->factor=2;
    else if(!strcmp(factor,"4"))c->factor=4;else if(!strcmp(factor,"8"))c->factor=8;
    else {*why="counter-unroll-unsupported";return FALSE;}
  }
  if(!path || !*path){*why="counter-contract-missing";return FALSE;}
  FILE *fp=fopen(path,"r");if(!fp){*why="counter-contract-open";return FALSE;}
  char line[512];unsigned row=0;bool good=TRUE;
  while(fgets(line,sizeof line,fp)) {
    size_t n=strlen(line);
    if(!n || (line[n-1]!='\n' && !feof(fp))){good=FALSE;break;}
    while(n && (line[n-1]=='\n' || line[n-1]=='\r'))line[--n]=0;
    if(row==0) {if(strcmp(line,"MX251-OUTER-COUNTER-V1")){good=FALSE;break;}}
    else if(row==1) {
      if(strncmp(line,"evidence=",9) || !m251CounterHex(line+9,64)){good=FALSE;break;}
      strcpy(c->evidence,line+9);
    } else {
      char *tab=strchr(line,'\t');
      if(!tab || strchr(tab+1,'\t') || c->count>=M251_COUNTER_MAX_TARGETS ||
         tab==line || (size_t)(tab-line)>=sizeof c->targets[0].target || !m251CounterHex(tab+1,8)){good=FALSE;break;}
      *tab++=0;
      if(!(isalpha((unsigned char)line[0]) || line[0]=='_')){good=FALSE;break;}
      for(unsigned i=0;line[i];++i)if(!isalnum((unsigned char)line[i]) && line[i]!='_'){good=FALSE;break;}
      if(!good)break;
      for(unsigned i=0;i<c->count;++i)if(!strcmp(c->targets[i].target,line)){good=FALSE;break;}
      if(!good)break;
      strcpy(c->targets[c->count].target,line);
      c->targets[c->count++].untouched=strtoul(tab,NULL,16);
    }
    ++row;
  }
  if(ferror(fp))good=FALSE;fclose(fp);
  if(!good || row<3 || !c->count){*why="counter-contract-malformed";return FALSE;}
  return TRUE;
}

/* 已由原 collector 谓词验证无调用/未知指令。此处再检查候选计数器的
 * 全部显式寄存器视图，读取也排除；不能仅根据目的操作数猜保存性质。 */
static bool m251CounterCollectorOperand(const char *operand,m251_outer_line *l,const m251_outer_region *r)
{
  if(!*operand)return FALSE;
  if(*operand=='#')return TRUE;
  if(m251OuterRegMask(operand))return TRUE;
  if(!strcmp(operand,"c") || !strcmp(operand,"dpl") || !strcmp(operand,"dph") ||
     !strcmp(operand,"dpxl") || !strcmp(operand,"dpx") || !strcmp(operand,"dptr") ||
     !strcmp(operand,"@dpx") || !strcmp(operand,"@dptr"))return TRUE;
  /* direct 数值/表达式可能命中低 RAM 寄存器别名。命名 direct 也不能
   * 猜作普通变量；仅允许原区域已经以绝对 SFR IR 验证的控制寄存器。 */
  for(unsigned i=r->control;i<r->control+4;++i) {
    char text[M251_OUTLINE_TEXT];m251OuterNormalize(l[i].node->line,text);
    char *p=NULL;
    if(!strncmp(text,"mov",3) || !strncmp(text,"clr",3))p=text+3;
    else if(!strncmp(text,"setb",4))p=text+4;
    if(p) {char *comma=strchr(p,',');if(comma)*comma=0;if(!strcmp(p,operand))return TRUE;}
  }
  return FALSE;
}

static unsigned long m251CounterCollectorTouched(const char *text,const char *label,
                                                 m251_outer_line *l,const m251_outer_region *r,bool *known)
{
  char anchor[80];snprintf(anchor,sizeof anchor,"%s:\n",label);
  const char *p=strstr(text,anchor);unsigned long mask=0;*known=FALSE;if(!p)return 0;
  p+=strlen(anchor);
  while(*p) {
    const char *q=strchr(p,'\n');if(!q)return 0;
    char line[M251_OUTLINE_TEXT],normalized[M251_OUTLINE_TEXT];size_t len=(size_t)(q-p);
    if(len>=sizeof line)return 0;memcpy(line,p,len);line[len]=0;m251OuterNormalize(line,normalized);
    if(!strcmp(normalized,"eret")){*known=TRUE;return mask;}
    if(strchr(normalized,':'))return 0;
    const char *args=line;while(isspace((unsigned char)*args))++args;
    while(isalpha((unsigned char)*args))++args;
    char operands[M251_OUTLINE_TEXT];m251OuterNormalize(args,operands);
    char *comma=strchr(operands,',');if(comma)*comma++=0;
    if(!m251CounterCollectorOperand(operands,l,r) ||
       (comma && !m251CounterCollectorOperand(comma,l,r)))return 0;
    for(const char *s=line;*s && *s!=';';) {
      if(!isalnum((unsigned char)*s) && *s!='_'){++s;continue;}
      char token[64];unsigned n=0;
      while(isalnum((unsigned char)*s) || *s=='_') {if(n+1>=sizeof token)return 0;token[n++]=*s++;}
      token[n]=0;mask|=m251OuterRegMask(token);
    }
    p=q+1;
  }
  return 0;
}

static unsigned m251CounterSelect(const m251_counter_contract *c,const char *target,unsigned long collectorMask)
{
  for(unsigned i=0;i<c->count;++i)if(!strcmp(c->targets[i].target,target)) {
    unsigned long mask=c->targets[i].untouched & ~collectorMask;
    if((mask&3UL)==3UL)return 0;
    if((mask&(3UL<<8))==(3UL<<8))return 8;
    return 32;
  }
  return 32;
}

static void m251CounterLabel(char *out,size_t size)
{ symbol *s=newiTempLabel(NULL);snprintf(out,size,"%05d$",labelKey2num(s->key)); }

static bool m251OuterCounterApply(m251_outer_line *l,unsigned count,m251_outer_region *regions,
                                 unsigned copies,const char *old,struct dbuf_s *changed,const char **why)
{
  m251_counter_contract *contract=Safe_alloc(sizeof(*contract));
  if(!m251CounterRead(contract,why)){Safe_free(contract);return FALSE;}
  m251_outer_region *r=regions;unsigned chosen[M251_OUTER_MAX_COPIES],groupCopies[2]={0,0};
  bool known=FALSE;unsigned long touched=m251CounterCollectorTouched(dbuf_c_str(changed),r->collector,l,r,&known);
  if(!known){*why="counter-collector-unknown";goto reject;}
  for(unsigned i=0;i<copies;++i) {
    m251_outer_region *x=regions+i;
    if(!x->nativeDual || x->innerHighByte || x->argBytes || x->counter!=4 || x->saved!=4 ||
       x->returned!=4 || x->result!=12 || x->sampleReg!=7 || x->indexReg!=6 ||
       !x->innerLimit || x->innerLimit%contract->factor || x->call!=x->inner+5 ||
       x->collect!=x->store+6) {*why="counter-shape-not-qualified";goto reject;}
    chosen[i]=m251CounterSelect(contract,x->target,touched);
    if(chosen[i]!=0 && chosen[i]!=8){*why="counter-no-untouched-word";goto reject;}
    ++groupCopies[chosen[i]==8];
  }
  /* 到此为止仅验证。后续不会失败；先改 collector 元数据，再生成新块。
   * 同一 collector 的所有物理入口仍被本次组完整拥有。 */
  unsigned groups=(groupCopies[0]!=0)+(groupCopies[1]!=0);
  if(groups>1) {
    const char *all=dbuf_c_str(changed),*mark=strstr(all,"; MX251 outer-collector ");
    const char *field=mark?strstr(mark,"\"newPhysicalCallers\":1"):NULL;
    if(!field){*why="counter-collector-metadata";goto reject;}
    struct dbuf_s revised;dbuf_init(&revised,dbuf_get_length(changed)+16);
    dbuf_append(&revised,all,(size_t)(field-all));dbuf_printf(&revised,"\"newPhysicalCallers\":%u",groups);
    dbuf_append_str(&revised,field+strlen("\"newPhysicalCallers\":1"));
    dbuf_set_length(changed,0);dbuf_append(changed,dbuf_get_buf(&revised),dbuf_get_length(&revised));dbuf_destroy(&revised);
  }
  char entries[2][64];
  for(unsigned g=0;g<2;++g)if(groupCopies[g]) {
    unsigned counter=g?8:0;char outer[64],inner[64],check[64],done[64],returnLabel[64],after[64];
    m251CounterLabel(entries[g],sizeof entries[g]);m251CounterLabel(outer,sizeof outer);m251CounterLabel(inner,sizeof inner);
    m251CounterLabel(check,sizeof check);m251CounterLabel(done,sizeof done);m251CounterLabel(returnLabel,sizeof returnLabel);m251CounterLabel(after,sizeof after);
    dbuf_printf(changed,"; MX251 outer-region {\"schema\":2,\"entry\":\"%s\",\"copies\":%u,\"collector\":\"%s\",\"sampleReg\":7,\"indexReg\":6,\"counter\":%u,\"sourceCounter\":4,\"saved\":6,\"result\":12,\"innerLimit\":%u,\"innerCompare\":\"word-cmp-jc\",\"outerLimit\":%u,\"argBytes\":0,\"calleeABI\":4,\"argumentRegisters\":[4,12],\"returnRegister\":4,\"stackDelta\":10,\"targetHigh\":6,\"targetLow\":4,\"highSlot\":%d,\"lowSlot\":%d,\"finalStorePolicy\":\"last-adjacent-region\",\"cleanupFlagReplay\":true,\"counterVariant\":\"preserved-word\",\"unroll\":%u,\"lastResultOnly\":true,\"sourceCounterReplay\":true,\"requiresActualTouchedProof\":true,\"requiresObserverAndEntryFlags\":true,\"evidence\":\"%s\"}\n",
      entries[g],groupCopies[g],r->collector,counter,r->innerLimit,r->outerLimit,r->highSlot,r->lowSlot,contract->factor,contract->evidence);
    dbuf_printf(changed,"%s:\n\tpush wr%u\n\tpush dr24\n\tpush ar6\n\tmov r7,#0\n%s:\n\tmov a,@spx\n\tadd a,r7\n\tmov r6,a\n",entries[g],counter,outer);
    for(unsigned i=r->control;i<r->control+4;++i)dbuf_printf(changed,"\t%s\n",l[i].node->line);
    dbuf_printf(changed,"\tpush wr6\n\tmov wr%u,#0\n%s:\n",counter,inner);
    for(unsigned repeat=0;repeat<contract->factor;++repeat) {
      for(unsigned i=r->inner+1;i<r->call;++i)dbuf_printf(changed,"\t%s\n",l[i].node->line);
      dbuf_append_str(changed,"\tmov wr24,@spx-0x0006\n\tmov wr26,@spx-0x0004\n\tecall @dr24\n");
    }
    dbuf_printf(changed,"\t%s wr%u,#%u\n\tcmp wr%u,#%u\n",contract->factor==8?"add":"inc",counter,contract->factor,counter,r->innerLimit);
    if(contract->factor==8)dbuf_printf(changed,"\tjnc %s\n\tejmp %s\n%s:\n",after,inner,after);
    else dbuf_printf(changed,"\tjc %s\n",inner);
    /* 原 PUSH/POP DR4 还保存了 WR6 的样本/索引；不能仅移走计数器后
     * 丢掉这半个保存。最后一次结果在重放原 WR4 前先快照到 DR12。 */
    dbuf_printf(changed,"\tmov dr12,dr4\n\tmov wr4,#%u\n\tpop wr6\n\t%s\n\tecall %s\n",r->innerLimit,l[r->collect-1].node->line,r->collector);
    dbuf_printf(changed,"\tcjne r7,#%u,%s\n%s:\n\tjnc %s\n\tejmp %s\n%s:\n\tdec spx,#4\n\tdec spx\n\tpop wr%u\n\tcjne r7,#%u,%s\n%s:\n\teret\n",r->outerLimit,check,check,done,outer,done,counter,r->outerLimit,returnLabel,returnLabel);
  }
  bool finalStores[M251_OUTER_MAX_COPIES];
  for(unsigned c=0;c<copies;++c)finalStores[c]=!(c+1<copies && m251OuterFinalStoreDead(l,count,regions+c,regions+c+1,old));
  for(unsigned c=0;c<copies;++c) {
    m251_outer_region *x=regions+c;const char *entry=entries[chosen[c]==8];unsigned slot=0;
    for(unsigned i=x->begin;i<=x->end;++i) {
      char text[M251_OUTLINE_TEXT];
      if(slot==0)snprintf(text,sizeof text,"mov wr26,#%s",x->target);
      else if(slot==1)snprintf(text,sizeof text,"movh dr24,#(%s >> 16)",x->target);
      else if(slot==2)snprintf(text,sizeof text,"mov r6,#%u",x->base);
      else if(slot==3)snprintf(text,sizeof text,"ecall %s",entry);
      else if(slot==4 && finalStores[c])snprintf(text,sizeof text,"%s",l[x->store].node->line);
      else if(slot==5 && finalStores[c])snprintf(text,sizeof text,"%s",l[x->store+1].node->line);
      else snprintf(text,sizeof text,"; MX251 outer-shared instruction");
      lineNode *node=l[i].node;node->line=Safe_strdup(text);node->isComment=slot>5 || (slot>=4 && !finalStores[c]);node->isLabel=FALSE;node->aln=NULL;++slot;
    }
    dbuf_printf(changed,"; MX251 outer-site {\"entry\":\"%s\",\"target\":\"%s\",\"base\":%u,\"sourceOuter\":\"%s\",\"sourceInner\":\"%s\",\"sourceExit\":\"%s\",\"objectKey\":%d,\"finalStore\":%s,\"nextSourceOuter\":\"%s\",\"counterVariant\":\"preserved-word\",\"counter\":%u,\"unroll\":%u}\n",entry,x->target,x->base,x->outerLabel,x->innerLabel,x->exitLabel,x->object->key,finalStores[c]?"true":"false",finalStores[c]?"":regions[c+1].outerLabel,chosen[c],contract->factor);
  }
  Safe_free(contract);return TRUE;
reject:
  Safe_free(contract);return FALSE;
}
