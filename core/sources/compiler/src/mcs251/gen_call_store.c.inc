/* GPL-2.0-or-later. 普通后端有界调用/存储区域的地址延后生成。
 * 不改 IR 或分配器；仅移纯地址式，删除其唯一成对保存，并重定基私有栈读。
 * FLAGS 的覆盖点按已经改写的后继重新核对；不跨调用/分支推断死亡。 */
#define M251_CALL_STORE_MAX_LINES 8192
#define M251_CALL_STORE_MAX_REGIONS 128
#define M251_CALL_STORE_MAX_REGION 48

typedef struct { lineNode *node; char text[M251_OUTLINE_TEXT]; } m251_store_line;
typedef struct {
  unsigned begin,prefixEnd,loads,call,restore,store,end,index;
  int offsets[4];unsigned loadRegs[4];const char *target;
  char base[256];
} m251_store_region;

static bool m251StoreSymbol(const char *s)
{
  if(!s || !(isalpha((unsigned char)*s) || *s=='_'))return FALSE;
  while(*++s)if(!(isalnum((unsigned char)*s) || *s=='_'))return FALSE;
  return TRUE;
}
static bool m251StorePrefix(m251_store_line *l,unsigned count,unsigned start,m251_store_region *r)
{
  unsigned k=start,idx,imm,n=0;char expected[M251_OUTLINE_TEXT];
  if(k>=count || !m251OuterMatch1(l[k++].text,"movwr4,wr%u%n",&idx) || idx>14 || (idx&1) ||
     idx==2 || idx==4 || idx==6 || idx==8 || idx==10 || idx==12)return FALSE;
  /* 可选的纯常数索引偏移；保留原指令，不重新结合可能溢出的运算。 */
  if(k<count && (m251OuterImm(l[k].text,"incwr4,#",&imm) || m251OuterImm(l[k].text,"addwr4,#",&imm)))++k;
  while(k<count && !strcmp(l[k].text,"addwr4,wr4") && n<3){++n;++k;}
  if(!n || k+3>=count || strncmp(l[k].text,"addwr4,#",8))return FALSE;
  const char *base=l[k].text+8;if(!m251StoreSymbol(base) || strlen(base)>=sizeof r->base)return FALSE;
  strcpy(r->base,base);++k;
  snprintf(expected,sizeof expected,"addca,#(%s>>16)",base);
  if(strcmp(l[k++].text,"clra") || strcmp(l[k++].text,expected) || strcmp(l[k++].text,"movr3,a"))return FALSE;
  r->begin=start;r->prefixEnd=k;r->index=idx;return TRUE;
}
static bool m251StoreHasIC(iCode *lic,iCode *wanted)
{
  if(!wanted)return FALSE;for(iCode *p=lic;p;p=p->next)if(p==wanted)return TRUE;return FALSE;
}
static bool m251StoreParse(m251_store_line *l,unsigned count,unsigned start,iCode *lic,m251_store_region *r,const char **why)
{
  memset(r,0,sizeof *r);if(!m251StorePrefix(l,count,start,r))return FALSE;
  unsigned k=r->prefixEnd,seen=0;char expected[64];
#define M251_STORE_EXPECT(s) do {if(k>=count || strcmp(l[k++].text,(s))){*why="shape-or-save-pair";return FALSE;}} while(0)
  M251_STORE_EXPECT("pushdr4");M251_STORE_EXPECT("pushar3");
  snprintf(expected,sizeof expected,"pushwr%u",r->index);M251_STORE_EXPECT(expected);
  r->loads=k;
  for(unsigned j=0;j<4;++j) {
    int off;unsigned reg,bit;
    if(k>=count || !m251OuterSlot(l[k++].text,FALSE,&off,&reg) || off>=-5 || off < -65535 ||
       (reg!=4 && reg!=6 && reg!=12 && reg!=14)){*why="argument-stack-word";return FALSE;}
    bit=reg==4?1:reg==6?2:reg==12?4:8;
    if(seen&bit){*why="duplicate-argument-register";return FALSE;}seen|=bit;r->offsets[j]=off;r->loadRegs[j]=reg;
  }
  r->call=k;if(k>=count || !m251OuterNativeCallee(l[k++].node,&r->target)){*why="callee-not-closed-native";return FALSE;}
  /* 返回值两个字的最终位置与索引不重叠；地址链仅改 DR4/R3/A。 */
  M251_STORE_EXPECT("movwr12,wr6");M251_STORE_EXPECT("movwr8,wr4");
  r->restore=k;snprintf(expected,sizeof expected,"popwr%u",r->index);M251_STORE_EXPECT(expected);
  M251_STORE_EXPECT("popar3");M251_STORE_EXPECT("popdr4");
  M251_STORE_EXPECT("movdpl,r5");M251_STORE_EXPECT("movdph,r4");M251_STORE_EXPECT("movdpxl,r3");
  r->store=k;M251_STORE_EXPECT("mov@dpx,wr8");M251_STORE_EXPECT("incdpx,#2");M251_STORE_EXPECT("mov@dpx,wr12");
  M251_STORE_EXPECT("incdpx");M251_STORE_EXPECT("mova,r13");r->end=k-1;
#undef M251_STORE_EXPECT
  if(k-start>=M251_CALL_STORE_MAX_REGION){*why="region-capacity";return FALSE;}
  /* 任何内部标签、inline asm、observable/atomic 操作或丢失当前IR来源均拒绝。
   * 不根据文本猜测 volatile MMIO；保留原两次 WR store 及其顺序。 */
  for(unsigned i=start;i<k;++i) {
    lineNode *p=l[i].node;
    if(p->isLabel || p->isInline || !m251StoreHasIC(lic,p->ic) ||
       (i!=r->call && !m251CallRegionIR(p))){*why="ir-or-observable-boundary";return FALSE;}
  }
  if(l[r->store].node->ic!=l[r->store+2].node->ic){*why="store-identity";return FALSE;}
  return TRUE;
}
/* 地址式跨 CALL 后移会改变算术 FLAGS。只接受后继在无调用/分支/未知动作
 * 前自行形成 ADD WR -> ADDC A,#bank 的覆盖链；MOV/INC 不冒充覆盖全部FLAGS。
 * 倒序应用，故本函数看到的已是其它候选改变后的后继，而不是旧死点。 */
static bool m251StoreClosedFlags(void)
{
  const char *v=getenv("MX251_DELAY_CALL_STORE_CLOSED_FLAGS");
  return v && !strcmp(v,"1");
}
static bool m251StoreWord(unsigned reg)
{return reg<=30 && !(reg&1);}
static bool m251StoreWordAdd(const char *text)
{
  unsigned a,b;int n=0;char immediate[256];
  if(m251OuterMatch2(text,"addwr%u,wr%u%n",&a,&b))return m251StoreWord(a)&&m251StoreWord(b);
  if(sscanf(text,"addwr%u,#%255s%n",&a,immediate,&n)!=2 || n<=0 || text[n] || !m251StoreWord(a))return FALSE;
  return m251StoreSymbol(immediate)||m251OuterImm(immediate,"",&b);
}
static bool m251StoreBankAdd(const char *text)
{
  size_t n=strlen(text);char base[256];
  if(n<14 || n-13>=sizeof base || strncmp(text,"addca,#(",8) || strcmp(text+n-5,">>16)"))return FALSE;
  memcpy(base,text+8,n-13);base[n-13]=0;return m251StoreSymbol(base);
}
static bool m251StoreFlagsDead(m251_store_line *l,unsigned count,unsigned start)
{
  bool carryDefined=FALSE,closed=m251StoreClosedFlags();unsigned budget=0;
  for(unsigned i=start;i<count && budget<32;++i) {
    lineNode *p=l[i].node;char text[M251_OUTLINE_TEXT];
    if(p->isComment || p->isDebug)continue;
    if(p->isLabel || p->isInline)return FALSE;++budget;m251OuterNormalize(p->line,text);
    if(m251StoreWordAdd(text)){carryDefined=TRUE;continue;}
    if(m251StoreBankAdd(text))return carryDefined;
    if(!strcmp(text,"clra"))continue;
    if(!strncmp(text,"incwr",5)) {
      unsigned reg,imm;
      if(m251OuterMatch2(text,"incwr%u,#%i%n",&reg,&imm) && m251StoreWord(reg) && imm>=1 && imm<=4)continue;
      return FALSE;
    }
    if(!strncmp(text,"movwr",5) || !strncmp(text,"movr",4)) {
      /* 仅寄存器/立即数；不得悄悄越过 PSW/SFR、内存、位或进位读取。 */
      char *comma=strchr(text,',');
      if(comma && !strchr(text,'@') && !strstr(text,"spx") && !strstr(text,"psw")) {
        char dst[32];size_t len=(size_t)(comma-text)-3;
        if(len<sizeof dst) {memcpy(dst,text+3,len);dst[len]=0;
          if(m251OuterRegMask(dst) && (comma[1]=='#' || m251OuterRegMask(comma+1)))continue;}
      }
    }
    if(closed) {
      /* 此开关只供已封闭镜像实验：host 必须另证当前所有目标及 ISR 不观察
       * PSW/SP/临时保存槽，并核对实际入口 FLAGS。签名不等于无观察者证明。
       * CALL 不是覆盖点，且其未知 FLAGS 会使此前 ADD 的证明失效。 */
      unsigned reg;int offset;const char *callee;
      if((m251OuterMatch1(text,"pushwr%u%n",&reg)||m251OuterMatch1(text,"popwr%u%n",&reg)) && m251StoreWord(reg))continue;
      if(m251OuterSlot(text,FALSE,&offset,&reg) && m251StoreWord(reg) && offset<=-1 && m251CallRegionIR(p))continue;
      if(m251OuterNativeCallee(p,&callee)){carryDefined=FALSE;continue;}
    }
    return FALSE;
  }
  return FALSE;
}
static void m251StoreJsonString(struct dbuf_s *out,const char *s)
{
  dbuf_append_char(out,'"');
  for(;*s;++s){if(*s=='"' || *s=='\\')dbuf_append_char(out,'\\');
    if(*s=='\t')dbuf_append_str(out,"\\t");else if(*s=='\n')dbuf_append_str(out,"\\n");else dbuf_append_char(out,*s);}
  dbuf_append_char(out,'"');
}
static void m251StoreApply(m251_store_line *l,const m251_store_region *r,unsigned serial)
{
  char text[M251_CALL_STORE_MAX_REGION][M251_OUTLINE_TEXT];iCode *ics[M251_CALL_STORE_MAX_REGION];unsigned n=0;
#define M251_STORE_COPY(i) do{snprintf(text[n],sizeof text[n],"%s",l[(i)].node->line);ics[n++]=l[(i)].node->ic;}while(0)
  /* DR4 同时保存 WR6；它可能是仍活跃的循环计数器，不能因地址只用 WR4
   * 就删除整组保存。只删除 WR4 与 bank R3 的三字节，保留 WR6 原值。 */
  snprintf(text[n],sizeof text[n],"push wr6");ics[n++]=l[r->prefixEnd].node->ic;
  M251_STORE_COPY(r->prefixEnd+2); /* 原索引 save，保留唯一匹配 pop。 */
  for(unsigned j=0;j<4;++j){int off=r->offsets[j]+3;snprintf(text[n],sizeof text[n],"mov wr%u,@spx-0x%04x",r->loadRegs[j],-off);ics[n++]=l[r->loads+j].node->ic;}
  for(unsigned i=r->call;i<=r->restore;++i)M251_STORE_COPY(i);
  snprintf(text[n],sizeof text[n],"pop wr6");ics[n++]=l[r->restore+2].node->ic;
  for(unsigned i=r->begin;i<r->prefixEnd;++i)M251_STORE_COPY(i);
  for(unsigned i=r->restore+3;i<=r->end;++i)M251_STORE_COPY(i);
#undef M251_STORE_COPY
  assert(n+2==r->end-r->begin+1);
  struct dbuf_s meta;dbuf_init(&meta,2048);
  dbuf_printf(&meta,"; MX251 delayed-call-store {\"schema\":3,\"id\":%u,\"target\":\"%s\",\"indexWR\":%u,\"preservedWR\":6,\"addressDR\":4,\"bankR\":3,\"stackBytesRemoved\":3,\"instructionDelta\":-2,\"knownByteDelta\":-4,\"flagProof\":\"%s\",\"requiresObserverQualification\":true,\"requiresEntryFlagsQualification\":true,\"prefixInstructions\":%u,\"base\":\"%s\",\"before\":[",serial,r->target,r->index,m251StoreClosedFlags()?"private-closed-calls-then-add-adc":"final-successor-add-adc-no-call",r->prefixEnd-r->begin,r->base);
  for(unsigned i=r->begin;i<=r->end;++i){if(i!=r->begin)dbuf_append_char(&meta,',');m251StoreJsonString(&meta,l[i].node->line);}
  dbuf_append_str(&meta,"],\"after\":[");for(unsigned i=0;i<n;++i){if(i)dbuf_append_char(&meta,',');m251StoreJsonString(&meta,text[i]);}dbuf_append_str(&meta,"]}");
  for(unsigned i=r->begin;i<=r->end;++i){lineNode *p=l[i].node;unsigned slot=i-r->begin;
    p->isLabel=FALSE;p->aln=NULL;
    if(slot==0){p->line=Safe_strdup(dbuf_c_str(&meta));p->isComment=TRUE;p->ic=NULL;}
    else if(slot<=n){p->line=Safe_strdup(text[slot-1]);p->isComment=FALSE;p->ic=ics[slot-1];}
    else {p->line=Safe_strdup("; MX251 delayed-call-store removed address save");p->isComment=TRUE;p->ic=NULL;}
  }
  dbuf_destroy(&meta);
}
static void mcs251DelayCallStoreAddresses(lineNode *head,iCode *lic)
{
  const char *flag=getenv("MX251_DELAY_CALL_STORE");if(!flag || strcmp(flag,"1"))return;
  bool trace=getenv("MX251_DELAY_CALL_STORE_TRACE")!=NULL;
  if(!currFunc || !lic || options.useXstack || FUNC_SDCCCALL(currFunc->type)!=2 ||
     IFFUNC_ISISR(currFunc->type) || IFFUNC_ISNAKED(currFunc->type) ||
     IFFUNC_CALLEESAVES(currFunc->type) || FUNC_REGBANK(currFunc->type))return;
  unsigned count=0;for(lineNode *p=head;p;p=p->next){if(p->isInline)return;if(!p->isComment && !p->isDebug)++count;}
  if(!count || count>M251_CALL_STORE_MAX_LINES)return;
  m251_store_line *l=Safe_alloc(count*sizeof(*l));unsigned at=0;
  for(lineNode *p=head;p;p=p->next)if(!p->isComment && !p->isDebug){l[at].node=p;m251OuterNormalize(p->line,l[at++].text);}
  m251_store_region regions[M251_CALL_STORE_MAX_REGIONS];unsigned found=0,accepted=0;const char *why="no-bounded-shape";
  for(unsigned i=0;i<count && found<M251_CALL_STORE_MAX_REGIONS;++i){m251_store_region r;
    if(m251StoreParse(l,count,i,lic,&r,&why)){regions[found++]=r;i=r.end;}}
  for(unsigned j=found;j>0;--j){m251_store_region *r=&regions[j-1];
    if(!m251StoreFlagsDead(l,count,r->end+1)){if(trace)fprintf(stderr,"delay-call-store reject target=%s reason=flags-not-dead-in-final-successor\n",r->target);continue;}
    m251StoreApply(l,r,++accepted);
  }
  if(trace)fprintf(stderr,"delay-call-store function=%s found=%u accepted=%u last=%s\n",currFunc->name,found,accepted,why);
  Safe_free(l);
}
