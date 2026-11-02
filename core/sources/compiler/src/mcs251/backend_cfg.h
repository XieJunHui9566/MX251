/* SPDX-License-Identifier: GPL-2.0-or-later
 * 用共同 IR 标签划分完整后端候选，枚举相邻标签间的机器路径。
 * 不猜分支频率或循环次数：每个入口/出口对，候选最坏成本必须不大于
 * 原方案最好成本。未配对的循环、调用次序、栈深或编码一律回退。
 * 这里只提供成本选择；不改 ABI、机器指令或控制流。 */
#ifndef MCS251_BACKEND_CFG_H
#define MCS251_BACKEND_CFG_H
#include <limits.h>
#include "wide_cost.h"
#include "backend_encoding.h"
#include "instruction_effects.h"
enum {BC_INS=2048,BC_LABELS=256,BC_NAME=256,BC_ROWS=1024,BC_CALLS=32};
/* 指标使用单独从零开始的枚举，避免容量常量进入数组下标。 */
enum {BCM_INS,BCM_READ,BCM_WRITE,BCM_PEAK,BCM_MEMORY,BCM_COUNT};
typedef struct {char name[BC_NAME];int at;} bc_label;
typedef struct {
  int next[2],delta,depth,anchor;unsigned metric[BCM_COUNT],call;
  char target[BC_NAME];unsigned branch,returned,tail;
} bc_instruction;
typedef struct {
  bc_instruction ins[BC_INS];bc_label labels[BC_LABELS];
  char calls[BC_CALLS][BC_NAME];unsigned ni,nl,nc,bytes;uint32_t writes;
} bc_graph;
typedef struct {
  int from,to;unsigned low[BCM_COUNT],high[BCM_COUNT],nc,call[BC_CALLS];
} bc_row;
typedef struct {bc_row rows[BC_ROWS];unsigned count,paths,steps;} bc_paths;

static int bc_copy(char *dst,const char *src,size_t n)
{if(!n || n>=BC_NAME)return 0;memcpy(dst,src,n);dst[n]=0;return 1;}
static int bc_cond(const char *op)
{
  const char *list[]={"jc","jnc","jz","jnz","je","jne","jg","jle","jsl","jsge","jsg","jsle","jb","jnb"};
  for(unsigned i=0;i<sizeof list/sizeof list[0];++i)if(!strcmp(op,list[i]))return 1;
  return 0;
}
static uint32_t bc_register(const char *s)
{
  mx251_effect_operand o;if(mx251EffectRegister(s,&o))return o.gpr;
  if(strlen(s)==3 && s[0]=='a' && s[1]=='r' && s[2]>='0' && s[2]<='7')return UINT32_C(1)<<(s[2]-'0');
  return 0;
}
static int bc_number(const char *s,int *out)
{
  if(*s++!='#' || !isdigit((unsigned char)*s))return 0;
  char *end;unsigned long n=strtoul(s,&end,0);if(*end || n>65535)return 0;
  *out=(int)n;return 1;
}
static int bc_parse(bc_graph *g,const char *text,size_t length,wide_cost_resolver resolver,const void *ctx)
{
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    const char *p=text+at,*last=text+end;at=end+1;
    while(p<last && isspace((unsigned char)*p))++p;
    const char *comment=memchr(p,';',(size_t)(last-p));if(comment)last=comment;
    while(last>p && isspace((unsigned char)last[-1]))--last;
    if(p==last)continue;
    if(last[-1]==':') {
      while(last>p && last[-1]==':')--last;
      if(g->nl==BC_LABELS || !bc_copy(g->labels[g->nl].name,p,(size_t)(last-p)))return 0;
      for(unsigned i=0;i<g->nl;++i)if(!strcmp(g->labels[i].name,g->labels[g->nl].name))return 0;
      g->labels[g->nl++].at=(int)g->ni;continue;
    }
    size_t n=(size_t)(last-p);char line[512];if(n>=sizeof line)return 0;
    memcpy(line,p,n);line[n]=0;unsigned ar,value;int used=0;
    if(sscanf(line,"ar%u = 0x%x%n",&ar,&value,&used)==2 && used==(int)n && ar<8 && value==ar)continue;
    unsigned bytes=bc_instruction_bytes(p,n,resolver,ctx);
    if(!bytes || g->ni==BC_INS)return 0;
    char op[24],args[512];size_t k=0,j=0;
    while(k<n && !isspace((unsigned char)line[k])) {if(k+1>=sizeof op)return 0;op[k]=(char)tolower((unsigned char)line[k]);++k;}
    op[k]=0;for(;k<n;++k)if(!isspace((unsigned char)line[k]))args[j++]=line[k];args[j]=0;
    char *second=strchr(args,',');if(second)*second++=0;
    bc_instruction *v=&g->ins[g->ni];v->next[0]=(int)g->ni+1;v->next[1]=-1;
    v->depth=INT_MIN;v->anchor=-1;v->metric[BCM_INS]=1;g->bytes+=bytes;
    v->metric[BCM_MEMORY]=strchr(args,'@')!=NULL || (second && strchr(second,'@')!=NULL);
    wide_cost_operand a=wide_cost_arg(args),b=second?wide_cost_arg(second):(wide_cost_operand){0};
    if(a.kind==WC_UNKNOWN && resolver)a=resolver(args,ctx);
    if(second && b.kind==WC_UNKNOWN && resolver)b=resolver(second,ctx);
    if(bc_cond(op) || !strcmp(op,"ejmp") || !strcmp(op,"sjmp")) {
      const char *target=args;
      if(!strcmp(op,"jb") || !strcmp(op,"jnb")) {if(a.kind!=WC_BIT || !second)return 0;target=second;}
      else if(second)return 0;
      if(*target=='@' || !bc_copy(v->target,target,strlen(target)))return 0;
      v->branch=bc_cond(op)?2:!strcmp(op,"ejmp")?3:1;
    } else if(op[0]=='j' || !strcmp(op,"djnz") || !strcmp(op,"cjne"))return 0;
    else if(!strcmp(op,"eret")) {v->returned=1;v->next[0]=-1;}
    else if(!strcmp(op,"ecall")) {
      if(second || *args=='@')return 0;
      unsigned c=0;for(;c<g->nc;++c)if(!strcmp(args,g->calls[c]))break;
      if(c==g->nc) {if(g->nc==BC_CALLS || !bc_copy(g->calls[g->nc],args,strlen(args)))return 0;++g->nc;}
      v->call=c+1;
    } else {
      if(strcmp(op,"push") && strcmp(op,"pushw") && strcmp(op,"cmp") && strcmp(op,"nop")) {
        g->writes|=bc_register(args);
        if(second && (!strcmp(op,"xch") || !strcmp(op,"mul") || !strcmp(op,"div")))g->writes|=bc_register(second);
      }
      if(!strcmp(op,"push") || !strcmp(op,"pushw") || !strcmp(op,"pop")) {
        int width=!strcmp(op,"pushw")?2:a.kind==WC_REG?a.width:1;
        if(!strcmp(op,"pop")){v->delta=-width;v->metric[BCM_READ]=(unsigned)width;}
        else {v->delta=width;v->metric[BCM_WRITE]=(unsigned)width;}
      } else if(!strcmp(args,"spx") || !strcmp(args,"dr60")) {
        if(strcmp(op,"add") && strcmp(op,"sub") && strcmp(op,"inc") && strcmp(op,"dec"))return 0;
        int amount=1;if(second && !bc_number(second,&amount))return 0;
        v->delta=(!strcmp(op,"add") || !strcmp(op,"inc"))?amount:-amount;
      } else if(!strcmp(op,"mov")) {
        if(strstr(args,"@spx") || strstr(args,"@dr60"))v->metric[BCM_WRITE]=b.kind==WC_REG?(unsigned)b.width:1;
        if(second && (strstr(second,"@spx") || strstr(second,"@dr60")))v->metric[BCM_READ]=a.kind==WC_REG?(unsigned)a.width:1;
      }
    }
    ++g->ni;
  }
  if(!g->ni)return 0;
  for(unsigned i=0;i<g->ni;++i) {
    bc_instruction *v=&g->ins[i];if(!v->branch)continue;
    int dest=-1;for(unsigned k=0;k<g->nl;++k)if(!strcmp(v->target,g->labels[k].name))dest=g->labels[k].at;
    if(dest<0 && v->branch==3) {
      /* 普通后端已生成的直接尾调用；仅与宽后端同路径同一 CALL 比较。
       * 不从任意表达式猜目标，也不在此把普通跳转改成尾调用。 */
      const char *t=v->target;if(!isalpha((unsigned char)*t) && *t!='_')return 0;
      for(;*t;++t)if(!isalnum((unsigned char)*t) && *t!='_' && *t!='$')return 0;
      unsigned c=0;for(;c<g->nc;++c)if(!strcmp(v->target,g->calls[c]))break;
      if(c==g->nc){if(g->nc==BC_CALLS)return 0;strcpy(g->calls[g->nc++],v->target);}
      v->call=c+1;v->returned=v->tail=1;v->next[0]=-1;
    } else {
      if(dest<0 || dest>=(int)g->ni)return 0;
      if(v->branch==2)v->next[1]=dest;else v->next[0]=dest;
    }
  }
  /* 所有可达汇合点必须具有唯一的物理栈深，返回深度为零。 */
  unsigned queue[BC_INS],begin=0,end=1;queue[0]=0;g->ins[0].depth=0;
  while(begin<end) {
    unsigned i=queue[begin++];bc_instruction *v=&g->ins[i];int depth=v->depth+v->delta;
    if(depth<0 || depth>65535 || (v->returned && depth))return 0;
    v->metric[BCM_PEAK]=(unsigned)(depth>v->depth?depth:v->depth)+(v->call && !v->tail?3:0);
    for(unsigned e=0;e<2;++e)if(v->next[e]>=0) {
      int nxt=v->next[e];if(nxt>=(int)g->ni)return 0;
      if(g->ins[nxt].depth!=INT_MIN) {if(g->ins[nxt].depth!=depth)return 0;}
      else {g->ins[nxt].depth=depth;queue[end++]=(unsigned)nxt;}
    }
  }
  return end==g->ni;
}
static int bc_label_at(const bc_graph *g,const char *name)
{for(unsigned i=0;i<g->nl;++i)if(!strcmp(name,g->labels[i].name))return g->labels[i].at;return -1;}
static int bc_record(bc_paths *paths,int from,int to,const unsigned *cost,const unsigned *calls,unsigned nc)
{
  if(++paths->paths>8192)return 0;
  unsigned k=0;for(;k<paths->count;++k)if(paths->rows[k].from==from && paths->rows[k].to==to)break;
  if(k==paths->count) {
    if(k==BC_ROWS)return 0;bc_row *r=&paths->rows[paths->count++];r->from=from;r->to=to;r->nc=nc;
    memcpy(r->call,calls,nc*sizeof *calls);memcpy(r->low,cost,BCM_COUNT*sizeof *cost);memcpy(r->high,cost,BCM_COUNT*sizeof *cost);return 1;
  }
  bc_row *r=&paths->rows[k];if(r->nc!=nc || memcmp(r->call,calls,nc*sizeof *calls))return 0;
  for(unsigned m=0;m<BCM_COUNT;++m){if(cost[m]<r->low[m])r->low[m]=cost[m];if(cost[m]>r->high[m])r->high[m]=cost[m];}
  return 1;
}
static int bc_walk(const bc_graph *g,bc_paths *p,int from,int at,unsigned steps,
  const unsigned *cost,const unsigned *calls,unsigned nc,unsigned char *seen)
{
  if(++p->steps>131072)return 0;
  if(at<0)return bc_record(p,from,-1,cost,calls,nc);
  const bc_instruction *v=&g->ins[at];
  if(steps && v->anchor>=0)return bc_record(p,from,v->anchor,cost,calls,nc);
  if(seen[at] || steps==BC_INS)return 0;seen[at]=1;
  unsigned next[BCM_COUNT],seq[BC_CALLS];memcpy(seq,calls,nc*sizeof *calls);
  for(unsigned m=0;m<BCM_COUNT;++m)next[m]=m==BCM_PEAK?(cost[m]>v->metric[m]?cost[m]:v->metric[m]):cost[m]+v->metric[m];
  if(v->call) {if(nc==BC_CALLS)return 0;seq[nc++]=v->call;}
  int ok=bc_walk(g,p,from,v->next[0],steps+1,next,seq,nc,seen);
  if(ok && v->next[1]>=0)ok=bc_walk(g,p,from,v->next[1],steps+1,next,seq,nc,seen);
  seen[at]=0;return ok;
}
static int bc_analyze(const bc_graph *g,bc_paths *p)
{
  unsigned cost[BCM_COUNT]={0},calls[BC_CALLS]={0};unsigned char seen[BC_INS]={0};
  for(unsigned i=0;i<g->ni;++i)if(g->ins[i].anchor>=0)
    if(!bc_walk(g,p,g->ins[i].anchor,(int)i,0,cost,calls,0,seen))return 0;
  return p->count!=0;
}
/* -1 未覆盖，0 已知拒绝，1 每个共同 IR 区域/出口均不退步。 */
static int bc_choose(const char *wide,size_t wn,const char *legacy,size_t ln,const char *function,
  const int *keys,unsigned count,wide_cost_resolver resolver,const void *ctx)
{
  int result=-1;bc_graph *w=calloc(1,sizeof *w),*l=calloc(1,sizeof *l);
  bc_paths *wp=calloc(1,sizeof *wp),*lp=calloc(1,sizeof *lp);
  if(!w || !l || !wp || !lp)goto done;
  if(!bc_parse(w,wide,wn,resolver,ctx) || !bc_parse(l,legacy,ln,resolver,ctx))goto done;
  if(l->bytes>=w->bytes || (l->writes & ~w->writes)){result=0;goto done;}
  w->ins[0].anchor=l->ins[0].anchor=0;unsigned anchors=1;
  for(unsigned i=0;i<count;++i) {
    char a[BC_NAME],b[BC_NAME];
    if(snprintf(a,sizeof a,"__wm_%s_%d",function,keys[i])>=(int)sizeof a)goto done;
    snprintf(b,sizeof b,"%05d$",keys[i]+100);
    int wi=bc_label_at(w,a),li=bc_label_at(l,b);
    if(wi<0 || li<0)continue;
    /* 无机器指令的末尾 IR 标签不是执行边界。指向末尾的实际跳转已在
     * bc_parse 中拒绝，不能把它误配到另一后端的返回指令。 */
    if(wi>=(int)w->ni || li>=(int)l->ni)continue;
    if(wi==0 || li==0) {if(wi!=li)goto done;continue;}
    if(w->ins[wi].anchor>=0 || l->ins[li].anchor>=0) {
      if(w->ins[wi].anchor==l->ins[li].anchor)continue;
      goto done;
    }
    w->ins[wi].anchor=l->ins[li].anchor=(int)anchors++;
  }
  if(!bc_analyze(w,wp) || !bc_analyze(l,lp))goto done;
  if(wp->count!=lp->count){result=0;goto done;}
  for(unsigned i=0;i<lp->count;++i) {
    const bc_row *a=&lp->rows[i],*b=NULL;
    for(unsigned j=0;j<wp->count;++j)if(wp->rows[j].from==a->from && wp->rows[j].to==a->to){b=&wp->rows[j];break;}
    if(!b || a->nc!=b->nc){result=0;goto done;}
    for(unsigned j=0;j<a->nc;++j)if(strcmp(l->calls[a->call[j]-1],w->calls[b->call[j]-1])){result=0;goto done;}
    for(unsigned m=0;m<BCM_COUNT;++m)if(a->high[m]>b->low[m]){result=0;goto done;}
  }
  result=1;
done: free(w);free(l);free(wp);free(lp);return result;
}
#endif
