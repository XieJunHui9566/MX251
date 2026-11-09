/* Exercise the production MIR copy pass with small, explicit control graphs.
 * Build: gcc -std=c99 -O2 value-copy-flow.c -o value-copy-flow.exe
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WV_MAX=512, WN_MAX=2048, WA_MAX=16, WB_WORDS=8,
       FUNCTION=256, CAST, WM_NOP, INPUT, BRANCH, TABLE, RESULT };
typedef struct { uint64_t w[WB_WORDS]; } wide_bits;
typedef struct { int pointer, space; } test_type;
typedef struct { test_type *type; } symbol;
typedef struct {
  symbol *sym; int width,pin;
  bool sign,pointer,codePointer,floating,nearData,symbolSnapshot;
} wide_value;
typedef struct { int value,width; unsigned literal; } wide_operand;
typedef struct {
  unsigned op,orderedReadCount,orderedAsmCount,addressOp,stackArgs;
  bool byteAccess; symbol *addressSymbol;
  int result,width,addressObject,succ[2],jumpCount,jumpStart;
  wide_operand left,right,addressIndex,stackOperands[WA_MAX];
  wide_bits use,def,out;
} wide_node;
typedef struct {
  int nv,nn,jumpSuccessors[WN_MAX];
  wide_value values[WV_MAX]; wide_node nodes[WN_MAX]; void *diagnostics;
} wide_function;
#define IS_PTR(t) ((t)->pointer)
#define DCL_TYPE(t) ((t)->space)
#define wassert assert
#define Safe_alloc malloc
#define Safe_free free
#define dbuf_printf(...) ((void)0)
static bool wb_has(wide_bits b,int n) { return n>=0 && (b.w[n/64] & (UINT64_C(1)<<(n%64))); }
static void wb_set(wide_bits *b,int n) { if(n>=0)b->w[n/64]|=UINT64_C(1)<<(n%64); }
static bool wb_equal(wide_bits a,wide_bits b) { return !memcmp(&a,&b,sizeof a); }
static wide_bits wb_or(wide_bits a,wide_bits b) { for(int i=0;i<WB_WORDS;++i)a.w[i]|=b.w[i];return a; }
static wide_bits wb_minus(wide_bits a,wide_bits b) { for(int i=0;i<WB_WORDS;++i)a.w[i]&=~b.w[i];return a; }
static void wide_reset_allocation(wide_function *f) { (void)f; }
static void wide_address_reachable(wide_function *f,int entry,int omitted,unsigned char *seen)
{
  int queue[WN_MAX],head=0,tail=0;memset(seen,0,WN_MAX);
  if(entry<0 || entry==omitted)return;
  queue[tail++]=entry;seen[entry]=1;
  while(head<tail) {
    wide_node *n=&f->nodes[queue[head++]];
    for(int k=0;k<2+n->jumpCount;++k) {
      int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
      if(to>=0 && to!=omitted && !seen[to]) {seen[to]=1;queue[tail++]=to;}
    }
  }
}
#include "../wide_value_copy.c.inc"

static wide_function original,optimized;
static unsigned checks;
static void init(int nodes)
{
  memset(&original,0,sizeof original);original.nv=8;original.nn=nodes;
  for(int i=0;i<8;++i)original.values[i]=(wide_value){.width=2,.pin=-1};
  for(int i=0;i<nodes;++i)original.nodes[i]=(wide_node){.op=WM_NOP,.result=-1,.width=2,
    .addressObject=-1,.succ={i+1<nodes?i+1:-1,-1},
    .left={.value=-1,.width=2},.right={.value=-1,.width=2},.addressIndex={.value=-1}};
  /* A conservative all-live set admits every test copy into the flow pass. */
  for(int i=0;i<nodes;++i)for(int v=0;v<8;++v)wb_set(&original.nodes[i].out,v);
  original.nodes[0].op=FUNCTION;
}
static void node(int i,int op,int result,int left,int right,unsigned literal)
{
  wide_node *n=&original.nodes[i];n->op=op;n->result=result;
  n->left.value=left;n->right.value=right;n->right.literal=literal;
  wb_set(&n->def,result);wb_set(&n->use,left);wb_set(&n->use,right);
}
static unsigned execute(wide_function *f,unsigned seed)
{
  unsigned v[WV_MAX]={0};int pc=0;
  for(int steps=0;steps<1000;++steps) {
    wide_node *n=&f->nodes[pc];
    unsigned a=n->left.value<0?0:v[n->left.value];
    unsigned b=n->right.value<0?n->right.literal:v[n->right.value];
    unsigned r=0;int next=n->succ[0];
    switch(n->op) {
    case INPUT:r=seed;break;
    case '=':case CAST:r=b;break;
    case '+':r=a+b;break;
    case '^':r=a^b;break;
    case BRANCH:if(a&b)next=n->succ[1];break;
    case TABLE:next=f->jumpSuccessors[n->jumpStart+(a%n->jumpCount)];break;
    case RESULT:return (a*257+b)&65535;
    default:break;
    }
    if(n->result>=0)v[n->result]=r&65535;
    assert(next>=0 && next<f->nn);pc=next;
  }
  assert(!"nonterminating test graph");return 0;
}
static void check(const char *name,int use,int expected,int copy,int removed)
{
  optimized=original;wide_value_copies(&optimized);
  if(use>=0)assert(optimized.nodes[use].left.value==expected);
  if(copy>=0)assert((optimized.nodes[copy].op==WM_NOP)==removed);
  for(unsigned seed=0;seed<256;++seed)assert(execute(&original,seed)==execute(&optimized,seed));
  ++checks;printf("PASS %s\n",name);
}
int main(void)
{
  init(5);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,'=',2,-1,1,0);node(4,RESULT,-1,2,1,0);
  check("copy chain",4,0,2,1);
  original.nodes[2].out=(wide_bits){{0}};
  optimized=original;wide_value_copies(&optimized);
  assert(optimized.nodes[2].op=='=');++checks;

  init(5);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  original.nodes[3].stackArgs=2;
  original.nodes[3].stackOperands[0]=(wide_operand){.value=1,.width=2};
  original.nodes[3].stackOperands[1]=(wide_operand){.value=0,.width=2};
  wb_set(&original.nodes[3].use,1);wb_set(&original.nodes[3].use,0);
  node(4,RESULT,-1,1,0,0);check("stack argument operands",4,0,2,1);
  assert(optimized.nodes[3].stackOperands[0].value==0);

  original.values[1].pin=4;
  check("fixed ABI destination",4,1,2,0);

  init(6);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,'+',0,0,-1,3);node(4,'+',2,1,-1,1);node(5,RESULT,-1,2,0,0);
  check("source redefinition",4,1,2,0);

  init(7);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,BRANCH,-1,0,-1,1);original.nodes[3].succ[1]=5;
  node(4,'+',0,0,-1,7);node(5,'+',2,1,-1,9);node(6,RESULT,-1,2,0,0);
  check("join with killed source",5,1,2,0);
  original.nodes[4].result=3;original.nodes[4].def=(wide_bits){{0}};wb_set(&original.nodes[4].def,3);
  check("join with unchanged source",5,0,2,1);

  init(9);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,'=',2,-1,-1,0);node(4,'+',3,1,2,0);node(5,'+',0,0,-1,1);
  node(6,'+',2,2,-1,1);node(7,BRANCH,-1,2,-1,4);
  original.nodes[7].succ[0]=4;original.nodes[7].succ[1]=8;
  node(8,RESULT,-1,3,0,0);check("loop backedge kills",4,1,2,0);
  node(5,'+',4,0,-1,1);original.nodes[5].def=(wide_bits){{0}};wb_set(&original.nodes[5].def,4);
  check("loop invariant copy",4,0,2,1);

  init(8);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,TABLE,-1,0,-1,0);original.nodes[3].succ[0]=-1;
  original.nodes[3].jumpCount=3;original.jumpSuccessors[0]=4;
  original.jumpSuccessors[1]=5;original.jumpSuccessors[2]=5;
  node(4,'+',0,0,-1,11);original.nodes[4].succ[0]=6;
  node(5,'+',3,0,-1,12);node(6,'+',2,1,-1,13);node(7,RESULT,-1,2,0,0);
  check("switch edges and duplicate targets",6,1,2,0);

  init(6);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,INPUT,2,-1,-1,0);original.values[0].symbolSnapshot=true;
  original.values[2].symbolSnapshot=true;node(4,'+',3,1,2,0);node(5,RESULT,-1,3,0,0);
  check("sampled values keep both reads",4,0,2,1);assert(optimized.nodes[1].op==INPUT && optimized.nodes[3].op==INPUT);

  init(5);node(1,INPUT,0,-1,-1,0);node(2,CAST,1,-1,0,0);
  original.values[1].width=1;original.nodes[2].width=1;
  node(3,'+',2,1,-1,1);node(4,RESULT,-1,2,0,0);
  /* Structural exclusion only: the tiny interpreter does not model CAST. */
  optimized=original;wide_value_copies(&optimized);assert(optimized.nodes[2].op==CAST);++checks;

  init(6);node(1,INPUT,0,-1,-1,0);node(2,'=',1,-1,0,0);
  node(3,'=',2,-1,1,0);node(4,'+',1,1,-1,1);node(5,RESULT,-1,2,1,0);
  check("copy destination with multiple definitions",5,2,2,0);
  printf("PASS %u graphs, 256 inputs per executable graph\n",checks);
  return 0;
}
