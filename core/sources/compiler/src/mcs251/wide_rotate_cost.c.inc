/* SPDX-License-Identifier: GPL-2.0-or-later
 * 局部路径证明：旧右移只抽取最高字节，旧左移的唯一回边次数是 literal。
 * 新方案没有回边；JNC 两条路的上界包含 INC，下界不包含 INC。
 * 每次到达此纯三节点链都执行一次，故局部最大值不超过旧精确值即可
 * 组合到任意外层路径；不重配寄存器，也不改其他指令或源 CFG。
 * 指标是动态指令和内存事务数，不声称芯片周期。未知编码/形态都拒绝。 */
typedef struct {unsigned instructions,mov,memory,reads,writes,branches,dec,jnz,jnc,inc,loopInit;bool unknown;} wide_rotate_metrics;

/* dbuf_get_buf 不承诺尾部 NUL。所有标记、字段和指令操作数只读
 * [begin,end)；容量中的旧内容不能参与候选选择或成本资格。 */
static bool
wide_rotate_prefix(const char **p,const char *end,const char *word)
{
  size_t n=strlen(word);
  if((size_t)(end-*p)<n || memcmp(*p,word,n))return false;
  *p+=n;return true;
}

static const char *
wide_rotate_find(const char *p,const char *end,const char *word)
{
  size_t n=strlen(word);
  while((size_t)(end-p)>=n) {
    if(!memcmp(p,word,n))return p;
    ++p;
  }
  return NULL;
}

static bool
wide_rotate_number(const char **p,const char *end,unsigned limit,unsigned *value)
{
  const char *q=*p;unsigned v=0;
  if(q==end || *q<'0' || *q>'9')return false;
  do {
    unsigned digit=(unsigned)(*q-'0');
    if(v>limit/10 || (v==limit/10 && digit>limit%10))return false;
    v=v*10+digit;++q;
  } while(q<end && *q>='0' && *q<='9');
  *p=q;*value=v;return true;
}

static bool
wide_rotate_header(const char *p,const char *end,unsigned *node,unsigned *count,unsigned *source,unsigned *result)
{
  /* MIR 索引由非负 int 生成；拒绝截断、溢出与多余字段。 */
  const unsigned indexMax=(unsigned)(~0u>>1);
  return wide_rotate_prefix(&p,end,"; rotate-proposal node=") && wide_rotate_number(&p,end,indexMax,node) &&
    wide_rotate_prefix(&p,end," count=") && wide_rotate_number(&p,end,7,count) && *count>0 &&
    wide_rotate_prefix(&p,end," source=") && wide_rotate_number(&p,end,indexMax,source) &&
    wide_rotate_prefix(&p,end," result=") && wide_rotate_number(&p,end,indexMax,result) && p==end;
}

static wide_rotate_metrics
wide_rotate_measure(const char *p,size_t length,unsigned count,unsigned candidate)
{
  wide_rotate_metrics m={0};
  if(!p || !length) {m.unknown=true;return m;}
  const char *start=p,*end=p+length;char operations[128][16]={{0}};
  unsigned compactRegister=64;
  while(p<end) {
    const char *next=memchr(p,'\n',(size_t)(end-p));if(!next) {m.unknown=true;break;}
    if(*p=='\t') {
      char op[16]={0};const char *args=p+1;while(args<next && !isspace((unsigned char)*args))++args;
      size_t n=(size_t)(args-p-1);if(!n || n>=sizeof op) {m.unknown=true;break;}memcpy(op,p+1,n);
      while(args<next && isspace((unsigned char)*args))++args;
      if(strcmp(op,"mov") && strcmp(op,"movz") && strcmp(op,"add") && strcmp(op,"sub") && strcmp(op,"orl") &&
         strcmp(op,"anl") && strcmp(op,"rl") && strcmp(op,"rr") && strcmp(op,"swap") &&
         strcmp(op,"inc") && strcmp(op,"dec") && strcmp(op,"jnz") && strcmp(op,"jnc"))m.unknown=true;
      if(m.instructions>=128) {m.unknown=true;break;}strcpy(operations[m.instructions],op);
      ++m.instructions;m.mov+=!strcmp(op,"mov");
      if(!strcmp(op,"sub")) {
        const char *q=args;unsigned a,b;
        if(!wide_rotate_prefix(&q,next,"wr") || !wide_rotate_number(&q,next,31,&a) ||
           !wide_rotate_prefix(&q,next,",wr") || !wide_rotate_number(&q,next,31,&b) || q!=next || a!=b)m.unknown=true;
      }
      if(candidate==2 && !strcmp(op,"add")) {
        const char *q=args;unsigned a,b;
        if(compactRegister!=64 || !wide_rotate_prefix(&q,next,"dr") ||
           !wide_rotate_number(&q,next,28,&a) || !wide_rotate_prefix(&q,next,",dr") ||
           !wide_rotate_number(&q,next,28,&b) || q!=next || a!=b || a%4 || a==8)m.unknown=true;
        else compactRegister=a;
      }
      if(candidate==2 && !strcmp(op,"inc")) {
        const char *q=args;unsigned low;
        if(!wide_rotate_prefix(&q,next,"wr") || !wide_rotate_number(&q,next,30,&low) ||
           !wide_rotate_prefix(&q,next,",#1") || q!=next || compactRegister==64 || low!=compactRegister+2)m.unknown=true;
      }
      const char *at=memchr(args,'@',(size_t)(next-args)),*comma=memchr(args,',',(size_t)(next-args));
      if(at) {++m.memory;if(!comma || strcmp(op,"mov") || next-at<4 || memcmp(at,"@spx",4))m.unknown=true;
        else if(at<comma)++m.writes;else ++m.reads;}
      if(!strcmp(op,"dec")) {++m.dec;if(next-args!=1 || *args!='a')m.unknown=true;}
      if(!strcmp(op,"jnz"))++m.jnz;if(!strcmp(op,"jnc"))++m.jnc;
      if(!strcmp(op,"inc"))++m.inc;
      if(!strcmp(op,"mov")) {char init[20];int size=snprintf(init,sizeof init,"a,#%u",count);
        if(next-args==size && !memcmp(args,init,(size_t)size))++m.loopInit;}
    } else if(*p!=';' && *p!='\n' && next[-1]!=':')m.unknown=true;
    p=next+1;
  }
  /* 只接受真实解析出的局部边：旧循环恰为 ADD/DEC/JNZ，候选 JNC
   * 只跳过紧随的一条 INC。形态变动不能沿用常量次数公式。 */
  struct dbuf_s graphText;dbuf_init(&graphText,256);dbuf_append_str(&graphText,"__rotate_fragment:\n");dbuf_append(&graphText,start,length);dbuf_append_str(&graphText,"\tnop\n");
  wlc_graph *g=Safe_alloc(sizeof *g);memset(g,0,sizeof *g);
  if(!wlc_parse_context(g,dbuf_get_buf(&graphText),dbuf_get_length(&graphText),NULL,NULL) || g->ni!=m.instructions+1)m.unknown=true;
  else for(unsigned i=0;i<g->ni;++i)if(g->ins[i].branch) {
    wlc_instruction *n=&g->ins[i];
    if(candidate==2) {
      if(!strcmp(n->opcode,"jnc")) {
        if(n->next[1]!=(int)i+2 || i+1>=g->ni || strcmp(operations[i+1],"inc"))m.unknown=true;
      } else if(strcmp(n->opcode,"jnz") || i<4 || n->next[1]!=(int)i-4 ||
        strcmp(operations[i-4],"add") || strcmp(operations[i-3],"jnc") ||
        strcmp(operations[i-2],"inc") || strcmp(operations[i-1],"dec"))m.unknown=true;
    } else if(candidate) {
      if(strcmp(n->opcode,"jnc") || n->next[1]!=(int)i+2 || i+1>=g->ni || strcmp(operations[i+1],"inc"))m.unknown=true;
    } else if(strcmp(n->opcode,"jnz") || i<2 || n->next[1]!=(int)i-2 ||
      strcmp(operations[i-2],"add") || strcmp(operations[i-1],"dec"))m.unknown=true;
  }
  Safe_free(g);dbuf_destroy(&graphText);
  if(candidate==2) {
    if(count<3 || count>7 || m.dec!=1 || m.jnz!=1 || m.jnc!=1 || m.inc!=1 || m.loopInit!=1)m.unknown=true;
    /* 已核对唯一回边与其内部唯一条件边；每轮 ADD/JNC/DEC/JNZ 必执行，
     * INC 至多一次。范围是动态指令数，不能替代芯片周期与完整工程计时。 */
    if(!m.unknown) {m.instructions+=5*(count-1);m.inc=count;}
  } else if(candidate) {
    if(m.dec || m.jnz || m.loopInit || m.jnc!=(count<=2?count:0) || m.inc!=(count<=2?count:0))m.unknown=true;
  } else {
    unsigned loop=count>4;
    if(m.dec!=loop || m.jnz!=loop || m.loopInit!=loop || m.jnc || m.inc)m.unknown=true;
    if(loop)m.instructions+=3*(count-1); /* ADD, DEC A, JNZ execute literal times. */
  }
  return m;
}

static unsigned
wide_rotate_finalize(wide_function *f,const char *text,size_t length,struct dbuf_s *output)
{
  if(!text || !length)return 0;
  const char *p=text,*end=text+length;unsigned selected=0;
  while(p<end) {
    const char *begin=wide_rotate_find(p,end,"; rotate-proposal ");if(!begin) {dbuf_append(output,p,(size_t)(end-p));break;}
    dbuf_append(output,p,(size_t)(begin-p));const char *line=memchr(begin,'\n',(size_t)(end-begin));
    unsigned count=0,node,source,result;
    if(!line || !wide_rotate_header(begin,line,&node,&count,&source,&result))return 0;
    struct dbuf_s candidate,compact;dbuf_init(&candidate,256);dbuf_init(&compact,256);const char *q=line+1;
    while(wide_rotate_prefix(&q,end,"; rotate-alt ")) {
      const char *next=memchr(q,'\n',(size_t)(end-q));if(!next) {dbuf_destroy(&compact);dbuf_destroy(&candidate);return 0;}
      dbuf_append(&candidate,q,(size_t)(next-q+1));q=next+1;
    }
    while(wide_rotate_prefix(&q,end,"; rotate-small ")) {
      const char *next=memchr(q,'\n',(size_t)(end-q));if(!next) {dbuf_destroy(&compact);dbuf_destroy(&candidate);return 0;}
      dbuf_append(&compact,q,(size_t)(next-q+1));q=next+1;
    }
    if(!wide_rotate_prefix(&q,end,"; rotate-original\n")) {dbuf_destroy(&compact);dbuf_destroy(&candidate);return 0;}
    const char *old=q,*finish=wide_rotate_find(old,end,"; rotate-end\n");if(!finish) {dbuf_destroy(&compact);dbuf_destroy(&candidate);return 0;}
    const char *newText=dbuf_get_buf(&candidate);size_t newLength=dbuf_get_length(&candidate),oldLength=(size_t)(finish-old);
    wide_byte_cost a=wide_emitted_bytes(f,old,oldLength),b=wide_emitted_bytes(f,newText,newLength);
    wide_rotate_metrics am=wide_rotate_measure(old,oldLength,count,false),bm=wide_rotate_measure(newText,newLength,count,true);
    bool use=!a.unknown && !b.unknown && !am.unknown && !bm.unknown && b.bytes<a.bytes &&
      bm.instructions<=am.instructions && bm.mov<=am.mov && bm.memory<=am.memory && bm.reads<=am.reads && bm.writes<=am.writes;
    dbuf_printf(f->diagnostics,"; wide rotate-emission choice function=%s node=%u count=%u selected=%u old-bytes=%u new-bytes=%u unknown=%u old-dynamic=%u new-min=%u new-max=%u old-read=%u new-read=%u old-write=%u new-write=%u frame=frozen allocation=frozen\n",currFunc->name,node,count,use,a.bytes,b.bytes,a.unknown||b.unknown||am.unknown||bm.unknown,am.instructions,bm.instructions-bm.inc,bm.instructions,am.reads,bm.reads,am.writes,bm.writes);
    bool useCompact=false;
    const char *smallText=dbuf_get_buf(&compact);size_t smallLength=dbuf_get_length(&compact);
    if(smallLength) {
      const char *enabled=getenv("MX251_COMPACT_ROTATE");
      wide_byte_cost sc=wide_emitted_bytes(f,smallText,smallLength);
      wide_rotate_metrics sm=wide_rotate_measure(smallText,smallLength,count,2);
      wide_byte_cost previous=use?b:a;wide_rotate_metrics pm=use?bm:am;
      /* 新配置允许已量化的速度/体积取舍，不改变默认快速配置，也不
       * 把未知成本当零。必须由完整工程实测判定最终双110%，此处不授予。 */
      useCompact=enabled && !strcmp(enabled,"1") && !previous.unknown && !pm.unknown &&
        !sc.unknown && !sm.unknown && sc.bytes<previous.bytes &&
        sm.memory<=pm.memory && sm.reads<=pm.reads && sm.writes<=pm.writes;
      dbuf_printf(f->diagnostics,"; wide rotate-compact choice node=%u count=%u selected=%u prior-bytes=%u compact-bytes=%u prior-min=%u prior-max=%u compact-min=%u compact-max=%u unknown=%u runtime-qualified=0\n",node,count,useCompact,previous.bytes,sc.bytes,pm.instructions-pm.inc,pm.instructions,sm.instructions-sm.inc,sm.instructions,previous.unknown||pm.unknown||sc.unknown||sm.unknown);
    }
    if(useCompact) {dbuf_printf(output,"; wide rotate-compact selected node=%u count=%u source=%u result=%u\n",node,count,source,result);dbuf_append(output,smallText,smallLength);++selected;}
    else if(use) {dbuf_printf(output,"; wide rotate-emission selected node=%u count=%u source=%u result=%u\n",node,count,source,result);dbuf_append(output,newText,newLength);++selected;}
    else dbuf_append(output,old,oldLength);
    dbuf_destroy(&compact);dbuf_destroy(&candidate);p=finish+13;
  }
  wide_byte_cost oldCost=wide_emitted_bytes(f,text,length),newCost=wide_emitted_bytes(f,dbuf_get_buf(output),dbuf_get_length(output));
  if(oldCost.unknown || newCost.unknown || newCost.bytes>=oldCost.bytes)return 0;
  return selected;
}
