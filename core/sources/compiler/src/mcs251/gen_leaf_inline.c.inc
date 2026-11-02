/* SPDX-License-Identifier: GPL-2.0-or-later
 * 有界闭合叶体展开。只复制契约中的原始寄存器指令，不改参数、保存或结果。
 * 契约来自当前闭合对象；宿主仍须重绑最终定义、调用边和机器码。 */
#include <stdint.h>

#define M251_LI_ENTRIES 32
#define M251_LI_INSNS 32
#define M251_LI_NAME 128
#define M251_LI_TEXT 96
#define M251_LI_WALK 65536

typedef struct {
  char name[M251_LI_NAME], sha[65];
  uint32_t hash;
  unsigned count, bytes;
  char body[M251_LI_INSNS][M251_LI_TEXT];
} m251_li_entry;
static m251_li_entry m251LiEntries[M251_LI_ENTRIES];
static unsigned m251LiCount;
static char m251LiEvidence[65];
static int m251LiLoaded;

static bool m251LiFail(const char *why)
{
  werror(E_INTERNAL_ERROR,__FILE__,__LINE__,why);
  return FALSE;
}
static bool m251LiHex(const char *s,unsigned n)
{
  if(strlen(s)!=n)return FALSE;
  for(unsigned i=0;i<n;++i)if(!isxdigit((unsigned char)s[i]))return FALSE;
  return TRUE;
}
static bool m251LiName(const char *s)
{
  if(!*s || !(isalpha((unsigned char)*s)||*s=='_'))return FALSE;
  for(++s;*s;++s)if(!(isalnum((unsigned char)*s)||*s=='_'))return FALSE;
  return TRUE;
}
static uint32_t m251LiHash(const char *s,uint32_t h)
{
  for(;*s;++s){h^=(unsigned char)*s;h*=UINT32_C(16777619);}
  return h;
}
static unsigned m251LiReg(const char *s)
{
  unsigned n=0,w;int used=0;
  if(!strncmp(s,"wr",2))w=2;
  else if(!strncmp(s,"dr",2))w=4;
  else return 0;
  if(!isdigit((unsigned char)s[2]) || strlen(s+2)>2 || (s[2]=='0' && s[3]))return 0;
  for(const char *p=s+2;*p;++p)if(!isdigit((unsigned char)*p))return 0;
  if(sscanf(s+2,"%u%n",&n,&used)!=1 || !used || s[2+used] || n%w || n>32-w)return 0;
  /* 不借助 A/R11、B/R10 或寄存器组别名；bank 属性另由 ABI 契约拒绝。 */
  if(n<=11 && n+w>10)return 0;
  return w;
}
/* 本轮有限语法：WR/DR 同宽寄存器算术及 MOV WR,#16 位常量。
 * 不接受符号表达式、内存、SFR、栈、分支、局部标签或隐式 FLAGS 读。 */
static bool m251LiInstruction(const char *text,char *canonical,unsigned *bytes)
{
  char op[16],a[32],b[32],compact[80];unsigned n=0,w,v;int used=0;
  const char *p=text;
  while(isspace((unsigned char)*p))++p;
  while(isalpha((unsigned char)*p)&&n+1<sizeof op)op[n++]=(char)tolower((unsigned char)*p++);
  op[n]=0;if(!n || !isspace((unsigned char)*p))return FALSE;
  n=0;for(;*p;++p)if(!isspace((unsigned char)*p)) {
    if(n+1>=sizeof compact)return FALSE;compact[n++]=(char)tolower((unsigned char)*p);
  }
  compact[n]=0;
  if(sscanf(compact,"%31[^,],%31s%n",a,b,&used)!=2 || used<=0 || compact[used])return FALSE;
  w=m251LiReg(a);if(!w)return FALSE;
  if(!strcmp(op,"mov") && !strncmp(b,"#0x",3)) {
    char *end=NULL;
    if(w!=2 || !b[3] || strlen(b)>7)return FALSE;
    for(const char *q=b+3;*q;++q)if(!isxdigit((unsigned char)*q))return FALSE;
    v=(unsigned)strtoul(b+3,&end,16);if(!end || *end || v>65535)return FALSE;
    snprintf(canonical,M251_LI_TEXT,"mov %s,#0x%04x",a,v);*bytes=4;return TRUE;
  }
  if(m251LiReg(b)!=w)return FALSE;
  if(strcmp(op,"mov") && strcmp(op,"add") && strcmp(op,"sub") &&
     !(w==2 && (!strcmp(op,"xrl")||!strcmp(op,"anl")||!strcmp(op,"orl"))))return FALSE;
  snprintf(canonical,M251_LI_TEXT,"%s %s,%s",op,a,b);*bytes=2;return TRUE;
}
static bool m251LiRead(FILE *f,char *line,size_t n)
{
  if(!fgets(line,(int)n,f))return FALSE;
  size_t len=strlen(line);
  if(!len || (line[len-1]!='\n' && !feof(f)))return FALSE;
  while(len && (line[len-1]=='\n'||line[len-1]=='\r'))line[--len]=0;
  return TRUE;
}
static m251_li_entry *m251LiFind(const char *name)
{
  for(unsigned i=0;i<m251LiCount;++i)if(!strcmp(name,m251LiEntries[i].name))return &m251LiEntries[i];
  return NULL;
}
static bool m251LiLoad(void)
{
  if(m251LiLoaded)return m251LiLoaded>0;
  m251LiLoaded=-1;
  const char *path=getenv("MX251_LEAF_INLINE_CONTRACT");
  FILE *f=path&&*path?fopen(path,"rb"):NULL;char line[512];bool ended=FALSE;
  if(!f)return m251LiFail("leaf inline contract missing or unreadable");
  if(!m251LiRead(f,line,sizeof line)||strcmp(line,"MX251-LEAF-INLINE-V1"))goto bad;
  if(!m251LiRead(f,line,sizeof line)||strncmp(line,"evidence=",9)||!m251LiHex(line+9,64))goto bad;
  strcpy(m251LiEvidence,line+9);
  while(m251LiRead(f,line,sizeof line)) {
    if(!strcmp(line,"end-contract")){ended=TRUE;break;}
    if(m251LiCount>=M251_LI_ENTRIES)goto bad;
    m251_li_entry *e=&m251LiEntries[m251LiCount];char fnv[9];int used=0;
    if(sscanf(line,"entry\t%127s\t%64s\t%8s\t%u\t%u%n",e->name,e->sha,fnv,&e->bytes,&e->count,&used)!=5 ||
       used<=0 || line[used] || !m251LiName(e->name)||!m251LiHex(e->sha,64)||!m251LiHex(fnv,8)||
       !e->count||e->count>M251_LI_INSNS || m251LiFind(e->name))goto bad;
    e->hash=(uint32_t)strtoul(fnv,NULL,16);unsigned total=0;uint32_t hash=UINT32_C(2166136261);
    for(unsigned j=0;j<e->count;++j) {
      unsigned bytes;
      if(!m251LiRead(f,line,sizeof line)||!m251LiInstruction(line,e->body[j],&bytes)||strcmp(line,e->body[j]))goto bad;
      total+=bytes;hash=m251LiHash(e->body[j],hash);hash=m251LiHash("\n",hash);
    }
    if(total!=e->bytes||hash!=e->hash||!m251LiRead(f,line,sizeof line)||strcmp(line,"end-entry"))goto bad;
    ++m251LiCount;
  }
  if(!ended || !m251LiCount || fgetc(f)!=EOF || ferror(f))goto bad;
  fclose(f);m251LiLoaded=1;return TRUE;
bad:
  fclose(f);return m251LiFail("leaf inline contract malformed or outside finite register grammar");
}
static bool m251LiNative(symbol *s)
{
  /* 迁移后的 ABI4 与源码本来显式采用 ABI4 的叶体都可进入合同。
   * 后者不在“ABI 迁移”名册内，仍须精确命中独立叶体合同；不能据此
   * 放行任意原生函数。最终宿主重绑实际定义、闭合边与复制机器字节。 */
  if(!s || !IS_FUNC(s->type) || FUNC_SDCCCALL(s->type)!=4 ||
     (!mcs251InternalNative(s) && !m251LiFind(s->rname)))return FALSE;
  sym_link *t=s->type;
  if(!IFFUNC_ISREENT(t)||IFFUNC_HASVARARGS(t)||IFFUNC_ISNAKED(t)||IFFUNC_CALLEESAVES(t)||
     IFFUNC_ISISR(t)||IFFUNC_ISCRITICAL(t)||IFFUNC_ISBANKEDCALL(t)||FUNC_REGBANK(t)||SPEC_NORETURN(getSpec(t)))return FALSE;
  if(!IS_INTEGRAL(t->next)||getSize(t->next)!=4)return FALSE;
  unsigned n=0;
  for(value *v=FUNC_ARGS(t);v;v=v->next)if(++n>2||!IS_INTEGRAL(v->type)||getSize(v->type)!=4)return FALSE;
  return n==2;
}
static bool m251LiIR(ebbIndex *ebbi,unsigned *calls)
{
  unsigned walked=0;
  if(!ebbi||ebbi->count<0||!ebbi->bbOrder)return FALSE;
  for(int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];bool reached;
    if(!bb||(!bb->sch!=!bb->ech))return FALSE;
    reached=!bb->sch;
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      if(++walked>M251_LI_WALK)return FALSE;
      if(ic==bb->ech)reached=TRUE;
      if(ic->op!=CALL || !IS_SYMOP(IC_LEFT(ic)))continue;
      symbol *callee=OP_SYMBOL(IC_LEFT(ic));m251_li_entry *e=callee?m251LiFind(callee->rname):NULL;
      if(!e)continue;
      if(!m251LiNative(callee)||ic->parmBytes)return FALSE;
      if(++calls[e-m251LiEntries]>256)return FALSE;
    }
    if(!reached)return FALSE;
  }
  return TRUE;
}
/* 只展开固定契约的第一层；新形成的叶体不加入字典。返回 false 禁止发布候选。 */
bool mcs251InlineClosedLeafText(struct dbuf_s *output,symbol *owner,ebbIndex *ebbi)
{
  const char *enabled=getenv("MX251_LEAF_INLINE");
  if(!enabled||strcmp(enabled,"1"))return TRUE;
  if(!m251LiLoad())return FALSE;
  if(!owner||!owner->rname[0])return m251LiFail("leaf inline has no function owner");
  m251_li_entry *definition=m251LiFind(owner->rname);
  unsigned irCalls[M251_LI_ENTRIES]={0},seen[M251_LI_ENTRIES]={0},bodyCount=0,labels=0,ordinal=0;
  bool returned=FALSE;
  if(!m251LiIR(ebbi,irCalls))return m251LiFail("leaf inline IR coverage or ABI guard incomplete");
  bool selected=definition!=NULL;
  for(unsigned i=0;i<m251LiCount;++i)selected|=irCalls[i]!=0;
  if(!selected)return TRUE;
  if(!mcs251InternalNative(owner)&&!definition)return m251LiFail("leaf inline owner outside closed native contract");
  if(definition&&!m251LiNative(owner))return m251LiFail("leaf inline definition ABI guard failed");
  size_t len=dbuf_get_length(output),pos=0;const char *text=dbuf_get_buf(output);
  struct dbuf_s rewritten;dbuf_init(&rewritten,len+512);bool ok=TRUE;
  while(pos<len) {
    size_t end=pos;while(end<len&&text[end]!='\n')++end;
    size_t next=end<len?end+1:end;char line[512],trim[512];size_t n=end-pos;
    if(n>=sizeof line) {
      /* 宽后端诊断注释允许长行；它们不是指令或可供拼接的代码。 */
      size_t q=pos;while(q<end&&isspace((unsigned char)text[q]))++q;
      if(q<end&&text[q]==';'){dbuf_append(&rewritten,text+pos,next-pos);pos=next;continue;}
      ok=FALSE;break;
    }
    memcpy(line,text+pos,n);line[n]=0;char *p=line;while(isspace((unsigned char)*p))++p;
    strcpy(trim,p);n=strlen(trim);while(n&&isspace((unsigned char)trim[n-1]))trim[--n]=0;
    bool blank=!n||trim[0]==';';
    if(definition&&!blank) {
      if(trim[n-1]==':') {
        trim[n-1]=0;
        if(!strcmp(trim,owner->rname)){if(labels++||bodyCount||returned){ok=FALSE;break;}}
        else if(strncmp(trim,"__wm_",5)||(!returned&&bodyCount)){ok=FALSE;break;}
      } else if(!strcmp(trim,"eret")) {
        if(returned||bodyCount!=definition->count){ok=FALSE;break;}returned=TRUE;
      } else {
        char canonical[M251_LI_TEXT];unsigned bytes;
        if(!labels||returned||bodyCount>=definition->count||!m251LiInstruction(trim,canonical,&bytes)||
           strcmp(canonical,definition->body[bodyCount++])){ok=FALSE;break;}
      }
    }
    char op[16],target[M251_LI_NAME];int used=0; m251_li_entry *e=NULL;
    if(!blank && sscanf(trim,"%15s %127s%n",op,target,&used)==2 && used>0&&!trim[used]&&!strcmp(op,"ecall"))e=m251LiFind(target);
    if(e) {
      unsigned idx=(unsigned)(e-m251LiEntries);
      if(definition || ++seen[idx]>irCalls[idx]){ok=FALSE;break;}
      dbuf_printf(&rewritten,"; MX251 leaf-inline {\"schema\":1,\"caller\":\"%s\",\"callee\":\"%s\",\"ordinal\":%u,\"originalABI\":4,\"originalCallBytes\":4,\"bodyBytes\":%u,\"bodyInstructions\":%u,\"bodySHA256\":\"%s\",\"bodyFNV32\":\"%08lx\",\"evidence\":\"%s\",\"originalRetained\":true,\"depth\":1}\n",
        owner->rname,e->name,++ordinal,e->bytes,e->count,e->sha,(unsigned long)e->hash,m251LiEvidence);
      for(unsigned j=0;j<e->count;++j)dbuf_printf(&rewritten,"\t%s\n",e->body[j]);
      dbuf_printf(&rewritten,"; MX251 leaf-inline-end ordinal=%u\n",ordinal);
    } else dbuf_append(&rewritten,text+pos,next-pos);
    pos=next;
  }
  for(unsigned i=0;i<m251LiCount;++i)if(seen[i]!=irCalls[i])ok=FALSE;
  if(definition&&(!returned||labels!=1||bodyCount!=definition->count))ok=FALSE;
  if(ok&&definition)dbuf_printf(&rewritten,"; MX251 leaf-inline-definition {\"schema\":1,\"function\":\"%s\",\"bodySHA256\":\"%s\",\"bodyFNV32\":\"%08lx\",\"verifiedExact\":true,\"originalRetained\":true}\n",definition->name,definition->sha,(unsigned long)definition->hash);
  if(ok){dbuf_set_length(output,0);dbuf_append(output,dbuf_get_buf(&rewritten),dbuf_get_length(&rewritten));}
  dbuf_destroy(&rewritten);
  return ok?TRUE:m251LiFail("leaf inline current body or semantic call count mismatches contract");
}
