/* SPDX-License-Identifier: GPL-2.0-or-later
 * 非嵌套循环的逐分支路径成本向量。输入是最终发射文本，不猜循环次数或周期。
 * 每个循环独立比较，禁止外围收益补偿循环内部退步。嵌套/路径预算超限/未知
 * 控制流明确返回 unknown；生产选择器沿用原成本决策，不授予新模型覆盖。
 * 独立负例门槛必须 known 且拒绝，unknown 不能算通过。不改变程序准入或 ABI。
 */
#ifndef MCS251_WIDE_LOOP_COST_H
#define MCS251_WIDE_LOOP_COST_H
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "wide_cost.h"

enum { WLC_INS=2048, WLC_LABELS=2048, WLC_LOOPS=32, WLC_NAME=160, WLC_BRANCHES=32, WLC_PATHS=128 };
enum { WLC_INSTRUCTIONS, WLC_MOV, WLC_MEMORY, WLC_STACK_READ, WLC_STACK_WRITE,
       WLC_PUSH, WLC_POP, WLC_METRICS };
typedef struct {
  char header[WLC_NAME]; unsigned metric[WLC_METRICS];
} wide_loop_cost;
typedef struct {
  char anchor[WLC_NAME], target[WLC_NAME], opcode[16]; unsigned ordinal;
} wlc_branch_identity;
typedef struct {
  unsigned loop, exited, metric[WLC_METRICS]; uint32_t seen,taken;
} wlc_path_cost;
typedef struct {
  unsigned unknown,count,branchCount,pathCount;
  wide_loop_cost loops[WLC_LOOPS];
  wlc_branch_identity branches[WLC_BRANCHES];
  wlc_path_cost paths[WLC_PATHS];
} wide_loop_costs;
typedef struct { char name[WLC_NAME];int at; } wlc_label;
typedef struct {
  char target[WLC_NAME];int next[2];unsigned metric[WLC_METRICS];
  unsigned branch,call,anchor,ordinal,site; char opcode[16];
} wlc_instruction;
typedef struct {
  wlc_instruction ins[WLC_INS];wlc_label labels[WLC_LABELS];
  unsigned ni,nl;int begin[WLC_LOOPS],end[WLC_LOOPS];
  unsigned char member[WLC_INS],visited[WLC_INS];
} wlc_graph;

static int wlc_copy(char *out,size_t capacity,const char *begin,size_t length)
{
  if(!length || length>=capacity)return 0;
  memcpy(out,begin,length);out[length]=0;return 1;
}

static int wlc_conditional(const char *op)
{
  static const char *names[]={"jc","jnc","jz","jnz","je","jne","jg","jle","jsl","jsge","jsg","jsle"};
  for(unsigned k=0;k<sizeof names/sizeof names[0];++k)if(!strcmp(op,names[k]))return 1;
  return 0;
}

static int wlc_parse_context(wlc_graph *g,const char *text,size_t length,wide_cost_resolver resolve,const void *context)
{
  unsigned ordinal=0;
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    const char *first=text+at,*last=text+end;
    while(first<last && isspace((unsigned char)*first))++first;
    const char *comment=memchr(first,';',(size_t)(last-first));if(comment)last=comment;
    while(last>first && isspace((unsigned char)last[-1]))--last;
    at=end+1;if(first==last)continue;
    if(last[-1]==':') {
      if(g->nl==WLC_LABELS || !wlc_copy(g->labels[g->nl].name,WLC_NAME,first,(size_t)(last-first-1)))return 0;
      for(unsigned k=0;k<g->nl;++k)if(!strcmp(g->labels[k].name,g->labels[g->nl].name))return 0;
      g->labels[g->nl++].at=(int)g->ni;ordinal=0;continue;
    }
    if(g->ni==WLC_INS)return 0;
    const char *space=first;while(space<last && !isspace((unsigned char)*space))++space;
    char op[16];if(!wlc_copy(op,sizeof op,first,(size_t)(space-first)))return 0;
    const char *args=space;while(args<last && isspace((unsigned char)*args))++args;
    /* 编码未知不按零计价。含未解析 direct symbol 的文本也保守保留旧方案。 */
    if(!wide_instruction_bytes_context(first,(size_t)(last-first),resolve,context))return 0;
    wlc_instruction *n=&g->ins[g->ni];n->next[0]=(int)g->ni+1;n->next[1]=-1;
    n->metric[WLC_INSTRUCTIONS]=1;n->metric[WLC_MOV]=!strcmp(op,"mov");
    n->metric[WLC_MEMORY]=memchr(args,'@',(size_t)(last-args))!=NULL;
    /* PUSHW 编码已在上方核对；一条隐式压栈不是零成本，也不冒充周期。 */
    int wordPush=strlen(op)==5 && tolower((unsigned char)op[0])=='p' &&
      tolower((unsigned char)op[1])=='u' && tolower((unsigned char)op[2])=='s' &&
      tolower((unsigned char)op[3])=='h' && tolower((unsigned char)op[4])=='w';
    n->metric[WLC_PUSH]=!strcmp(op,"push") || wordPush;n->metric[WLC_POP]=!strcmp(op,"pop");
    char operands[512];if(args<last && !wlc_copy(operands,sizeof operands,args,(size_t)(last-args)))return 0;
    if(args==last)operands[0]=0;
    char *stack=strstr(operands,"@spx"),*comma=strchr(operands,',');
    if(stack) {
      if(!comma || strcmp(op,"mov"))return 0;
      n->metric[stack<comma?WLC_STACK_WRITE:WLC_STACK_READ]=1;
    }
    n->call=!strcmp(op,"ecall");
    if(n->call && !wlc_copy(n->target,WLC_NAME,args,(size_t)(last-args)))return 0;
    if(!strcmp(op,"eret"))n->next[0]=-1;
    else if(!strcmp(op,"ejmp") || !strcmp(op,"sjmp") || wlc_conditional(op)) {
      n->branch=wlc_conditional(op)?2:1;
      if(n->branch==2) {
        if(!g->nl)return 0;
        n->anchor=g->nl-1;n->ordinal=ordinal++;
        if(!wlc_copy(n->opcode,sizeof n->opcode,op,strlen(op)))return 0;
      }
      if(!wlc_copy(n->target,WLC_NAME,args,(size_t)(last-args)))return 0;
    }
    ++g->ni;
  }
  if(!g->ni)return 0;
  for(unsigned i=0;i<g->ni;++i) {
    wlc_instruction *n=&g->ins[i];
    if(n->next[0]==(int)g->ni)n->next[0]=-1;
    if(n->branch) {
      int target=-1;
      for(unsigned k=0;k<g->nl;++k)if(!strcmp(n->target,g->labels[k].name))target=g->labels[k].at;
      if(target<0 || target>=(int)g->ni)return 0;
      if(n->branch==2)n->next[1]=target;else n->next[0]=target;
    }
  }
  return 1;
}

typedef int (*wide_callee_cost_resolver)(const char *,const void *,unsigned [WLC_METRICS]);
/* 只规范化发射器的条件跳板，绝不删除其成本：
 *   inverse skip; jump destination; skip:
 * 与直接条件跳转的逻辑 taken/fallthrough 对齐，但 jump 仍是原图中
 * 独立计价节点。跳板中间有标签、外入边或额外指令时保持旧图。
 * 旧入口默认不启用此能力；未知形态仍须能与另一图逐路径匹配。 */
static void wlc_canonical_condition_trampolines(wlc_graph *g)
{
  static const char *pairs[][2]={{"jc","jnc"},{"je","jne"},{"jz","jnz"},
    {"jg","jle"},{"jsl","jsge"},{"jsg","jsle"}};
  for(unsigned i=0;i+2<g->ni;++i) {
    wlc_instruction *n=&g->ins[i],*jump=&g->ins[i+1];
    if(n->branch!=2 || n->next[0]!=(int)i+1 || n->next[1]!=(int)i+2 ||
       jump->branch!=1 || jump->next[1]>=0 || jump->next[0]<0 ||
       jump->next[0]==(int)i+1 || jump->next[0]==(int)i+2)continue;
    int private=1;
    for(unsigned l=0;l<g->nl;++l)if(g->labels[l].at==(int)i+1)private=0;
    for(unsigned from=0;from<g->ni;++from)for(unsigned edge=0;edge<2;++edge)
      if(g->ins[from].next[edge]==(int)i+1 && !(from==i && edge==0))private=0;
    if(!private)continue;
    const char *inverse=NULL;
    for(unsigned p=0;p<sizeof pairs/sizeof pairs[0];++p)for(unsigned j=0;j<2;++j)
      if(!strcmp(n->opcode,pairs[p][j]))inverse=pairs[p][1-j];
    if(!inverse)continue;
    strcpy(n->opcode,inverse);strcpy(n->target,jump->target);
    n->next[0]=(int)i+2;n->next[1]=(int)i+1;
  }
}

static wide_loop_costs wide_loop_cost_analyze_context_calls_mode(const char *text,size_t length,wide_cost_resolver resolve,const void *context,
  wide_callee_cost_resolver callee,const void *callContext,int canonicalBranches)
{
  wide_loop_costs result={0};wlc_graph *g=(wlc_graph *)calloc(1,sizeof *g);
  if(!g) {result.unknown=1;return result;}
  if(!wlc_parse_context(g,text,length,resolve,context))goto unknown;
  if(canonicalBranches)wlc_canonical_condition_trampolines(g);
  for(unsigned i=0;i<g->ni;++i)for(unsigned edge=0;edge<2;++edge) {
    int target=g->ins[i].next[edge];
    if(target<0 || target>(int)i)continue;
    if(result.count==WLC_LOOPS)goto unknown;
    unsigned k=result.count++;
    g->begin[k]=target;g->end[k]=(int)i;
    /* 相交区间可能为嵌套、多回边或不可约循环，当前不作不可靠的频率推断。 */
    for(unsigned j=0;j<k;++j)if(target<=g->end[j] && (int)i>=g->begin[j])goto unknown;
    if(!wlc_copy(result.loops[k].header,WLC_NAME,g->ins[i].target,strlen(g->ins[i].target)))goto unknown;
  }
  for(unsigned k=0;k<result.count;++k) {
    int begin=g->begin[k],end=g->end[k];
    memset(g->member,0,sizeof g->member);memset(g->visited,0,sizeof g->visited);
    g->member[end]=1;
    /* 区间内除已识别回边外均前向；逆序找能到达 latch 的节点，排除 exit trampoline。 */
    for(int i=end-1;i>=begin;--i)for(unsigned edge=0;edge<2;++edge) {
      int next=g->ins[i].next[edge];
      if(next>=begin && next<=end && g->member[next])g->member[i]=1;
    }
    if(!g->member[begin])goto unknown;
    for(unsigned i=0;i<g->ni;++i)if(!g->member[i])for(unsigned edge=0;edge<2;++edge) {
      int next=g->ins[i].next[edge];
      if(next>=0 && g->member[next] && next!=begin)goto unknown;
    }
    /* 分支身份来自同一发射器的标签、条件序号、条件码和目标。
     * 不按指令位置或寄存器编号匹配，重着色/去spill不会改变路径身份。
     * 条件重排或CFG结构改变会返回unknown，不用最大值把不同路径配成一对。 */
    for(int i=begin;i<=end;++i)if(g->member[i] && g->ins[i].branch==2) {
      wlc_instruction *n=&g->ins[i];
      if(result.branchCount==WLC_BRANCHES)goto unknown;
      n->site=++result.branchCount;
      wlc_branch_identity *id=&result.branches[n->site-1];
      strcpy(id->anchor,g->labels[n->anchor].name);strcpy(id->target,n->target);
      strcpy(id->opcode,n->opcode);id->ordinal=n->ordinal;
    }
    /* 只在分支处分叉，显式有界工作栈避免按指令深度递归。 */
    typedef struct { int at;unsigned done,steps;wlc_path_cost path; } walk_state;
    walk_state pending[WLC_BRANCHES+1]={{0}};unsigned waiting=1,completed=0;
    pending[0].at=begin;pending[0].path.loop=k;
    while(waiting) {
      walk_state state=pending[--waiting];
      while(!state.done) {
        int current=state.at;wlc_instruction *n=&g->ins[current];
        if(state.steps++>=WLC_INS)goto unknown;
        if(n->call) {
          unsigned metrics[WLC_METRICS];
          if(!callee || !callee(n->target,callContext,metrics))goto unknown;
          for(unsigned m=0;m<WLC_METRICS;++m) {
            if(metrics[m]>UINT_MAX-state.path.metric[m])goto unknown;
            state.path.metric[m]+=metrics[m];
          }
        }
        g->visited[current]=1;
        for(unsigned m=0;m<WLC_METRICS;++m) {
          if(n->metric[m]>UINT_MAX-state.path.metric[m])goto unknown;
          state.path.metric[m]+=n->metric[m];
        }
        /* 每条路径保留独立成本。出口只计至离开循环成员集合的边；
         * 后续exit trampoline/函数尾部不在此循环迭代模型内。 */
        unsigned edges=n->next[1]>=0 && n->next[1]!=n->next[0]?2:1;
        if(n->branch==2)state.path.seen|=UINT32_C(1)<<(n->site-1);
        walk_state successors[2]={state,state};
        for(unsigned edge=0;edge<edges;++edge) {
          walk_state *next=&successors[edge];int target=n->next[edge];
          if(edge)next->path.taken|=UINT32_C(1)<<(n->site-1);
          if(target==begin) {if(current!=end)goto unknown;next->done=2;}
          else if(target<0 || !g->member[target]) {next->done=1;next->path.exited=1;}
          else next->at=target;
        }
        if(edges==2) {
          if(waiting==WLC_BRANCHES+1)goto unknown;
          pending[waiting++]=successors[1];
        }
        state=successors[0];
      }
      if(result.pathCount==WLC_PATHS)goto unknown;
      for(unsigned p=0;p<result.pathCount;++p) {
        const wlc_path_cost *old=&result.paths[p];
        if(old->loop==k && old->seen==state.path.seen && old->taken==state.path.taken && old->exited==state.path.exited)goto unknown;
      }
      result.paths[result.pathCount++]=state.path;
      if(!state.path.exited) {
        ++completed;
        /* 仅供诊断的逐指标上界。实际准入比较paths，不比较此最大值。 */
        for(unsigned m=0;m<WLC_METRICS;++m)
          if(state.path.metric[m]>result.loops[k].metric[m])result.loops[k].metric[m]=state.path.metric[m];
      }
    }
    if(!completed)goto unknown;
    for(int i=begin;i<=end;++i)if(g->member[i]!=g->visited[i])goto unknown;
  }
  free(g);return result;
unknown:
  result.unknown=1;free(g);return result;
}

static wide_loop_costs wide_loop_cost_analyze_context_calls(const char *text,size_t length,wide_cost_resolver resolve,const void *context,
  wide_callee_cost_resolver callee,const void *callContext)
{return wide_loop_cost_analyze_context_calls_mode(text,length,resolve,context,callee,callContext,0);}

/* 旧无上下文入口保持保守；生产新候选只传入真实符号的地址空间事实，
 * 未知/外部/越界符号仍由原编码器返回未知，不能猜一个地址使门禁通过。 */
static wide_loop_costs wide_loop_cost_analyze_context(const char *text,size_t length,wide_cost_resolver resolve,const void *context)
{return wide_loop_cost_analyze_context_calls(text,length,resolve,context,NULL,NULL);}
static wide_loop_costs wide_loop_cost_analyze(const char *text,size_t length)
{return wide_loop_cost_analyze_context(text,length,NULL,NULL);}

/* -1 未覆盖，0 已知循环退步，1 已知逐循环非退步。不能把 -1 当作实测或通过。 */
static int wide_loop_cost_decide(const wide_loop_costs *oldCost,const wide_loop_costs *newCost)
{
  if(oldCost->unknown || newCost->unknown || oldCost->count!=newCost->count ||
     oldCost->branchCount!=newCost->branchCount || oldCost->pathCount!=newCost->pathCount)return -1;
  for(unsigned k=0;k<oldCost->count;++k)
    if(strcmp(oldCost->loops[k].header,newCost->loops[k].header))return -1;
  for(unsigned k=0;k<oldCost->branchCount;++k) {
    const wlc_branch_identity *a=&oldCost->branches[k],*b=&newCost->branches[k];
    if(a->ordinal!=b->ordinal || strcmp(a->anchor,b->anchor) ||
       strcmp(a->target,b->target) || strcmp(a->opcode,b->opcode))return -1;
  }
  int matched[WLC_PATHS];
  for(unsigned p=0;p<oldCost->pathCount;++p) {
    const wlc_path_cost *a=&oldCost->paths[p];matched[p]=-1;
    for(unsigned q=0;q<newCost->pathCount;++q) {
      const wlc_path_cost *b=&newCost->paths[q];
      if(a->loop==b->loop && a->seen==b->seen && a->taken==b->taken && a->exited==b->exited) {matched[p]=(int)q;break;}
    }
    if(matched[p]<0)return -1;
  }
  for(unsigned p=0;p<oldCost->pathCount;++p)for(unsigned m=0;m<WLC_METRICS;++m)
    if(newCost->paths[matched[p]].metric[m]>oldCost->paths[p].metric[m])return 0;
  return 1;
}

static int wide_loop_cost_compare(const char *oldText,size_t oldLength,const char *newText,size_t newLength)
{
  wide_loop_costs oldCost=wide_loop_cost_analyze(oldText,oldLength);
  wide_loop_costs newCost=wide_loop_cost_analyze(newText,newLength);
  return wide_loop_cost_decide(&oldCost,&newCost);
}
static int wide_loop_cost_compare_context(const char *oldText,size_t oldLength,
  const char *newText,size_t newLength,wide_cost_resolver resolve,const void *context)
{
  wide_loop_costs oldCost=wide_loop_cost_analyze_context(oldText,oldLength,resolve,context);
  wide_loop_costs newCost=wide_loop_cost_analyze_context(newText,newLength,resolve,context);
  return wide_loop_cost_decide(&oldCost,&newCost);
}
static int wide_loop_cost_compare_callees(const char *oldText,size_t oldLength,
 const char *newText,size_t newLength,wide_cost_resolver operand,wide_callee_cost_resolver callee,
 const void *oldContext,const void *newContext)
{
 wide_loop_costs a=wide_loop_cost_analyze_context_calls(oldText,oldLength,operand,oldContext,callee,oldContext);
 wide_loop_costs b=wide_loop_cost_analyze_context_calls(newText,newLength,operand,newContext,callee,newContext);
 return wide_loop_cost_decide(&a,&b);
}

static int wide_loop_cost_compare_canonical(const char *oldText,size_t oldLength,
 const char *newText,size_t newLength,wide_cost_resolver operand,wide_callee_cost_resolver callee,
 const void *oldContext,const void *newContext)
{
 wide_loop_costs a=wide_loop_cost_analyze_context_calls_mode(oldText,oldLength,operand,oldContext,callee,oldContext,1);
 wide_loop_costs b=wide_loop_cost_analyze_context_calls_mode(newText,newLength,operand,newContext,callee,newContext,1);
 return wide_loop_cost_decide(&a,&b);
}
#endif
