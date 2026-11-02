/* SPDX-License-Identifier: GPL-2.0-or-later
 * Source 模式普通后端的局部控制流规范化。只删除无副作用的跳转，
 * 保留所有数据/volatile/调用指令的顺序，不改变调用宽度或栈约定。
 * 分支距离复用目标的 Source 模式指令上界；未知指令、伪操作成为屏障。
 * 不处理跳转表、内联汇编、裸函数或中断函数；候选默认启用，同一实现同时服务两种后端。
 */
static bool m251CfgWideMode;
static unsigned m251CfgWideSerial;
static bool m251CfgLocal (const char *s)
{
  if(!strncmp(s,"__wm_",5))return TRUE;
  unsigned n=0; while (s[n]>='0' && s[n]<='9') ++n;
  return n>=5 && s[n]=='$' && !s[n+1];
}
static bool m251CfgParse (lineNode *p, char *op, char *args)
{
  const char *s=p->line; unsigned a=0,b=0;
  if (!s || p->isLabel || p->isComment || p->isDebug) return FALSE;
  while (isspace((unsigned char)*s)) ++s;
  while (*s && !isspace((unsigned char)*s) && *s!=';') {
    if(a>=15) return FALSE; op[a++]=*s++;
  }
  op[a]=0;
  while (*s && *s!=';') {
    if(!isspace((unsigned char)*s)) { if(b>=255) return FALSE; args[b++]=*s; }
    ++s;
  }
  args[b]=0; return a>0;
}
static void m251CfgSet (lineNode *p,const char *op,const char *args)
{
  char text[288]; SNPRINTF(text,sizeof text,"%s\t%s",op,args);
  p->line=Safe_strdup(text);p->aln=NULL;
}
static void m251CfgDelete (lineNode *p)
{
  p->line=Safe_strdup("; MX251 local CFG removed");p->isComment=TRUE;p->isLabel=FALSE;p->aln=NULL;
}
static lineNode *m251CfgNext (lineNode *p)
{
  while(p && (p->isComment || p->isDebug || p->isLabel))p=p->next;
  return p;
}
static lineNode *m251CfgLabel (lineNode *head,const char *label)
{
  lineNode *found=NULL;size_t len=strlen(label);
  if(!m251CfgLocal(label))return NULL;
  for(lineNode *p=head;p;p=p->next)if(p->isLabel && !strncmp(p->line,label,len) && p->line[len]==':') {
    if(found)return NULL;found=p;
  }
  return found;
}
static bool m251CfgGoto (const char *op)
{ return !strcmp(op,"ejmp") || !strcmp(op,"sjmp"); }
static bool m251CfgBranch (const char *op)
{
  static const char *names[]={"ejmp","sjmp","jc","jnc","jz","jnz","jb","jnb","jbc","cjne","djnz","je","jne","jg","jle","jsg","jsge","jsl","jsle"};
  for(unsigned i=0;i<sizeof names/sizeof names[0];++i)if(!strcmp(op,names[i]))return TRUE;
  return FALSE;
}
static unsigned m251CfgRegisterWidth (const char *s)
{
  /* B 的物理别名是 R10，但汇编拼写 b 是直接 SFR；不能按 R10 编码计长。 */
  if (!strcmp (s, "a")) return 1;
  unsigned width = 1; unsigned long n; char *end;
  if (*s == 'w') { width = 2; ++s; }
  else if (*s == 'd') { width = 4; ++s; }
  if (*s++ != 'r' || !isdigit ((unsigned char)*s)) return 0;
  n = strtoul (s, &end, 10);
  return !*end && n <= (width == 1 ? 15 : 32-width) && !(n % width) ? width : 0;
}
static unsigned m251CfgBound (lineNode *p)
{
  char op[16],args[256];
  if(p->isComment || p->isDebug || p->isLabel)return 0;
  if(!m251CfgParse(p,op,args))return 1000000;
  const char *converge = getenv ("MX251_CFG_CONVERGE");
  if (!converge || !strcmp (converge, "1")) {
    /* 仅缩紧已核对 as251 Source-mode 编码的视图；其余仍用原上界。
     * 特别不把直接地址/位地址当寄存器，也不猜符号的地址宽度。 */
    if (!strcmp (op, "mov")) {
      char operands[256]; strcpy (operands, args); char *src = strchr (operands, ',');
      if (src) {
        *src++ = 0; unsigned w = m251CfgRegisterWidth (operands);
        if (w && w == m251CfgRegisterWidth (src)) return 2;
        if (w == 1 && *src == '#') return !strcmp (operands, "a") || !strcmp (operands, "r11") ? 2 : 3;
      }
    }
    if ((!strcmp (op, "clr") || !strcmp (op, "cpl")) && !strcmp (args, "a")) return 1;
  }
  if(m251CfgBranch(op))return (unsigned)port->peep.getSize(p);
  static const char *names[]={"mov","movh","movs","movz","movc","movx","push","pushw","pop","ecall","lcall","acall","ret","eret","reti","nop","add","addc","sub","subb","cmp","inc","dec","mul","div","anl","orl","xrl","clr","setb","cpl","rl","rlc","rr","rrc","sra","srl","sll","swap","da","xch","xchd"};
  for(unsigned i=0;i<sizeof names/sizeof names[0];++i)if(!strcmp(op,names[i])) {
    int size=port->peep.getSize(p);return size>0 && size<=6?(unsigned)size:1000000;
  }
  return 1000000;
}
static bool m251CfgRange (lineNode *head,lineNode *from,const char *label)
{
  lineNode *to=m251CfgLabel(head,label),*p;unsigned bytes=0;
  if(!to)return FALSE;
  for(p=from->next;p && p!=to && bytes<=127;p=p->next)bytes+=m251CfgBound(p);
  if(p==to && bytes<=127)return TRUE;
  /* 向后范围计入分支自身的 6 B 上界。所有本 pass 改写只缩短。 */
  bytes=6;
  for(p=to;p && p!=from && bytes<=128;p=p->next)bytes+=m251CfgBound(p);
  return p==from && bytes<=128;
}
static bool m251CfgReferenced (lineNode *head,lineNode *label,const char *name,const char *helpers)
{
  if(helpers && strstr(helpers,name))return TRUE;
  for(lineNode *p=head;p;p=p->next)
    if(p!=label && !p->isComment && !p->isDebug && strstr(p->line,name))return TRUE;
  return FALSE;
}
/* 合并的是同一函数内逐条相同、终点相同的尾部；不增加 CALL/栈帧。
 * 仅在严格字节下界盈利时合并。可能增加一次跳转，不能当作零周期代价。 */
static unsigned m251CfgTail (lineNode *end,lineNode **nodes,char keys[][288])
{
  unsigned count=0;
  for(lineNode *p=end;p && count<24;p=p->prev) {
    char op[16],args[256];
    if(p->isComment || p->isDebug)continue;
    if(p->isLabel || !m251CfgParse(p,op,args) || m251CfgBound(p)>6)break;
    if(count && (m251CfgBranch(op) || !strcmp(op,"eret") || !strcmp(op,"ret") || !strcmp(op,"reti")))break;
    nodes[count]=p;
    SNPRINTF(keys[count],288,"%s %s",m251CfgGoto(op)?"goto":op,args);++count;
  }
  return count;
}
static unsigned m251CfgMinimum (lineNode *p)
{
  char op[16],args[256];m251CfgParse(p,op,args);
  if(!strcmp(op,"ecall") && *args!='@')return 4;
  if(!strcmp(op,"ejmp"))return 4;
  if(!strcmp(op,"sjmp") || !strcmp(op,"push") || !strcmp(op,"pop"))return 2;
  /* 对照 as251 Source 模式编码：MOV 最少 2 B；非 A 的立即数
   * 搬运最少 3 B（WR/DR 立即数为 4 B，但这里只取下界）。 */
  if(!strcmp(op,"mov"))return strstr(args,",#") && strncmp(args,"a,",2) && strncmp(args,"r11,",4)?3:2;
  if(!strcmp(op,"movh"))return 4;
  return 1;
}
static void m251CfgShareTails (lineNode *head, unsigned minimumGain)
{
  for(unsigned round=0;round<64;++round) {
    lineNode *bestA[24],*bestB[24];unsigned bestN=0,bestGain=minimumGain;
    for(lineNode *a=head;a;a=a->next) {
      char ao[16],aa[256];
      if(!m251CfgParse(a,ao,aa) || (!m251CfgGoto(ao) && strcmp(ao,"eret")))continue;
      lineNode *an[24];char ak[24][288];unsigned ac=m251CfgTail(a,an,ak);
      for(lineNode *b=a->next;b;b=b->next) {
        char bo[16],ba[256];
        if(!m251CfgParse(b,bo,ba) || (!m251CfgGoto(bo) && strcmp(bo,"eret")))continue;
        lineNode *bn[24];char bk[24][288];unsigned bc=m251CfgTail(b,bn,bk),n=0,bytes=0;
        while(n<ac && n<bc && !strcmp(ak[n],bk[n])) {bytes+=m251CfgMinimum(bn[n]);++n;}
        if(n<2 || bytes<=4 || bytes-4<=bestGain)continue;
        bool overlap=FALSE;
        for(unsigned i=0;i<n;++i)for(unsigned j=0;j<n;++j)if(an[i]==bn[j])overlap=TRUE;
        if(overlap)continue;
        bestN=n;bestGain=bytes-4;
        memcpy(bestA,an,n*sizeof(*an));memcpy(bestB,bn,n*sizeof(*bn));
      }
    }
    if(!bestN)break;
    symbol *sym=newiTempLabel(NULL);char target[64],label[72];
    if(m251CfgWideMode) SNPRINTF(target,sizeof target,"__wm_cfg_%u",++m251CfgWideSerial);
    else SNPRINTF(target,sizeof target,"%05d$",labelKey2num(sym->key));
    SNPRINTF(label,sizeof label,"%s:",target);
    lineNode *start=bestA[bestN-1],*ln=newLineNode(label);
    ln->isLabel=TRUE;ln->ic=start->ic;
    ln->prev=start->prev;ln->next=start;if(start->prev)start->prev->next=ln;start->prev=ln;
    m251CfgSet(bestB[bestN-1],"ejmp",target);
    for(unsigned i=0;i+1<bestN;++i)m251CfgDelete(bestB[i]);
  }
}
static void mcs251CleanupLocalCfg (lineNode *head,struct dbuf_s *helpers)
{
  const char *enabled=getenv("MX251_LOCAL_CFG");
  if((enabled && strcmp(enabled,"1")) || options.debug || !currFunc || IFFUNC_ISNAKED(currFunc->type) ||
     IFFUNC_ISISR(currFunc->type) || IFFUNC_CALLEESAVES(currFunc->type) || FUNC_REGBANK(currFunc->type))return;
  for(lineNode *p=head;p;p=p->next)
    if(p->isInline || (p->ic && (p->ic->op==JUMPTABLE || p->ic->op==CRITICAL || p->ic->op==ENDCRITICAL)))return;
  /* 变参调用的参数区是构建契约：不把调用移到其参数元数据之外。 */
  for(lineNode *p=head;p;p=p->next)
    if(p->ic && p->ic->op==CALL && IC_LEFT(p->ic) &&
       IFFUNC_HASVARARGS(operandType(IC_LEFT(p->ic))))return;
  /* helpers 可以来自 dbuf_append/收缩重写；有效长度外不属于文本。
   * 在字符串引用扫描前建立终止符；分配失败时保守不做 CFG 改写。 */
  const char *helperText = dbuf_c_str (helpers);
  if (!helperText) return;
  const char *converge = getenv ("MX251_CFG_CONVERGE");
  bool finish = !converge || !strcmp (converge, "1");
  /* 共享会产生新的跳转/标号，之后再次规范化才能暴露短跳和公共尾部。
   * 八次有界布局阶段，最后只收缩跳转；不增加调用或改变栈帧。
   * 小尾部仅在此显式候选启用，每次必须有严格的编码下界收益。 */
  for (unsigned phase = 0; phase < (finish ? 8U : 1U); ++phase) {
  /* 有界固定点；跳转环不展开，所有变化保持本函数的原始入口/数据指令。 */
  for(unsigned round=0;round<32;++round) {
    bool changed=FALSE;
    for(lineNode *p=head;p;p=p->next) {
      char op[16],args[256];
      if(!m251CfgParse(p,op,args) || !m251CfgBranch(op))continue;
      char *target=strrchr(args,',');target=target?target+1:args;
      lineNode *label=m251CfgLabel(head,target),*q=label?m251CfgNext(label->next):NULL;
      char qo[16],qa[256];
      if(!q || q==p || !m251CfgParse(q,qo,qa))continue;
      if(m251CfgGoto(op) && !strcmp(qo,"eret") && !*qa) {
        m251CfgSet(p,"eret","");changed=TRUE;continue;
      }
      /* 只跨过一条纯跳转；自环/双节点环不会被改写。 */
      if(m251CfgGoto(qo) && m251CfgLocal(qa) && strcmp(target,qa)) {
        lineNode *nextLabel=m251CfgLabel(head,qa);
        if(nextLabel && m251CfgNext(nextLabel->next)!=p &&
           (!strcmp(op,"ejmp") || m251CfgRange(head,p,qa))) {
          strcpy(target,qa);m251CfgSet(p,op,args);changed=TRUE;
        }
      }
    }
    for(lineNode *p=head;p;p=p->next) {
      char op[16],args[256];
      if(!m251CfgParse(p,op,args) || !m251CfgGoto(op))continue;
      lineNode *label=m251CfgLabel(head,args);
      if(label && m251CfgNext(p->next)==m251CfgNext(label->next)) {m251CfgDelete(p);changed=TRUE;}
      else if(!strcmp(op,"ejmp") && m251CfgRange(head,p,args)) {m251CfgSet(p,"sjmp",args);changed=TRUE;}
    }
    for(lineNode *p=head;p;p=p->next) {
      char op[16],args[256],jo[16],ja[256];const char *inverse=NULL;
      if(!m251CfgParse(p,op,args))continue;
      if(!strcmp(op,"jz"))inverse="jnz";else if(!strcmp(op,"jnz"))inverse="jz";
      else if(!strcmp(op,"jc"))inverse="jnc";else if(!strcmp(op,"jnc"))inverse="jc";
      if (finish && !inverse) {
        if(!strcmp(op,"je"))inverse="jne";else if(!strcmp(op,"jne"))inverse="je";
        else if(!strcmp(op,"jg"))inverse="jle";else if(!strcmp(op,"jle"))inverse="jg";
        else if(!strcmp(op,"jsg"))inverse="jsle";else if(!strcmp(op,"jsle"))inverse="jsg";
        else if(!strcmp(op,"jsge"))inverse="jsl";else if(!strcmp(op,"jsl"))inverse="jsge";
      }
      if(!inverse)continue;
      lineNode *jump=m251CfgNext(p->next),*label=m251CfgLabel(head,args);
      if(!jump || !label || !m251CfgParse(jump,jo,ja) || !m251CfgGoto(jo) ||
         m251CfgNext(jump->next)!=m251CfgNext(label->next) || !m251CfgRange(head,p,ja))continue;
      /* 不跨越有其他入口的标号，避免删除仍被别处执行的跳转。 */
      bool entry=FALSE;for(lineNode *q=p->next;q && q!=jump;q=q->next)if(q->isLabel)entry=TRUE;
      if(entry)continue;
      m251CfgSet(p,inverse,ja);m251CfgDelete(jump);changed=TRUE;
    }
    /* 删除无引用局部标号后，纯跳转/返回与下一入口之间的指令确为不可达。
     * 任何地址物化或未知伪操作引用都由文本引用检查保守保留。 */
    for(lineNode *p=head;p;p=p->next)if(p->isLabel) {
      char name[64];const char *end=strchr(p->line,':');
      if(!end || end-p->line>=sizeof name)continue;
      memcpy(name,p->line,end-p->line);name[end-p->line]=0;
      if(m251CfgLocal(name) && !m251CfgReferenced(head,p,name,helperText)){m251CfgDelete(p);changed=TRUE;}
    }
    bool dead=FALSE;
    for(lineNode *p=head;p;p=p->next) {
      char op[16],args[256];
      if(p->isLabel) {dead=FALSE;continue;}
      if(p->isComment || p->isDebug)continue;
      if(!m251CfgParse(p,op,args) || m251CfgBound(p)>6) {dead=FALSE;continue;}
      if(dead){m251CfgDelete(p);changed=TRUE;continue;}
      dead=m251CfgGoto(op) || !strcmp(op,"eret");
    }
    if(!changed)break;
  }
  const char *tails=getenv("MX251_LOCAL_TAIL_SHARE");
  if((!tails || !strcmp(tails,"1")) && (!finish || phase < 7))m251CfgShareTails(head, finish ? 0 : 5);
  }
}

/* 原 helpers 在 line list 之后输出，早先的跳转整理看不到其最终布局。
 * 将同一函数的已生成文本并入列表，保持字面次序和标签，不改变 helper
 * 调用约定。只对显式开启的候选执行；未开启时保留原输出路径。 */
static void mcs251FinishOutlinedCfg (lineNode *head, struct dbuf_s *helpers)
{
  const char *enabled = getenv ("MX251_CFG_CONVERGE");
  if ((enabled && strcmp (enabled, "1")) || !head || !dbuf_get_length (helpers)) return;
  lineNode *tail = head; while (tail->next) tail = tail->next;
  /* 不把旧分配区残留当作仍有效的 helper 指令复制到列表。 */
  const char *helperText = dbuf_c_str (helpers);
  if (!helperText) return;
  char *text = Safe_strdup (helperText), *start = text;
  while (*start) {
    char *end = strchr (start, '\n'); if (end) *end = 0;
    const char *trim = start; while (isspace ((unsigned char)*trim)) ++trim;
    if (*trim) {
      lineNode *p = newLineNode (start);
      p->isComment = *trim == ';';
      p->isLabel = !p->isComment && strchr (trim, ':') != NULL;
      p->prev = tail; tail->next = p; tail = p;
    }
    if (!end) break; start = end + 1;
  }
  dbuf_set_length (helpers, 0); Safe_free (text);
  mcs251CleanupLocalCfg (head, helpers);
}

/* 两种后端共用已分配指令上的控制流整理。只接受编译器可见的普通函数，
 * 不把不透明汇编、跳转表或临界区的隐含入口交给文本级变换。 */
void mcs251CleanupWideCfg (struct dbuf_s *output, ebbIndex *ebbi)
{
  const char *enabled=getenv("MX251_LOCAL_CFG");
  if ((enabled && strcmp(enabled,"1")) || options.nopeep || options.debug || !currFunc ||
      IFFUNC_ISNAKED(currFunc->type) || IFFUNC_ISISR(currFunc->type) ||
      IFFUNC_CALLEESAVES(currFunc->type) || FUNC_REGBANK(currFunc->type)) return;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next)
      if(ic->op==INLINEASM || ic->op==JUMPTABLE || ic->op==CRITICAL || ic->op==ENDCRITICAL)return;
      else if(ic->op==CALL && IC_LEFT(ic) && IFFUNC_HASVARARGS(operandType(IC_LEFT(ic))))return;
  }
  char *text=Safe_strdup(dbuf_c_str(output)),*line=text;
  /* 旧后端的数字局部标号已在原路径整理；不插入会改变其作用域的全局标号。 */
  for(const char *p=text;*p;) {
    while(isspace((unsigned char)*p))++p;
    const char *q=p;while(isdigit((unsigned char)*q))++q;
    if(q-p>=5 && q[0]==36 && q[1]==58) {Safe_free(text);return;}
    p=strchr(p,10);if(!p)break;++p;
  }
  lineNode *head=newLineNode("; mx251-shared-cfg wide final allocation"),*tail=head;
  head->isComment=TRUE;
  while(*line) {
    char *end=strchr(line,'\n');if(end)*end=0;
    while(isspace((unsigned char)*line))++line;
    if(*line) {
      lineNode *p=newLineNode(line);p->isComment=*line==';';
      size_t n=strlen(line);p->isLabel=!p->isComment && n && line[n-1]==':';
      tail->next=p;p->prev=tail;tail=p;
    }
    if(!end)break;line=end+1;
  }
  struct dbuf_s helpers;dbuf_init(&helpers,16);
  m251CfgWideMode=true;mcs251CleanupLocalCfg(head,&helpers);m251CfgWideMode=false;
  dbuf_set_length(output,0);printLine(head,output);dbuf_destroy(&helpers);
  while(head) {lineNode *next=head->next;Safe_free(head->line);Safe_free(head->aln);Safe_free(head);head=next;}
  Safe_free(text);
}
