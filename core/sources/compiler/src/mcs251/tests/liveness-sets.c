/* Differential test of production liveness/interference against pairwise
 * construction, including word boundaries, loops and repeated switch edges.
 * Build: gcc -std=c99 -O2 liveness-sets.c -o liveness-sets.exe
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WV_MAX=1024, WB_WORDS=16, WN_MAX=32 };
typedef struct { uint64_t w[WB_WORDS]; } wide_bits;
typedef struct { wide_bits edges; } wide_value;
typedef struct { wide_bits in,out,use,def;int succ[2],jumpStart,jumpCount; } wide_node;
typedef struct {
  int nv,nn,jumpSuccessors[WN_MAX*3];
  wide_node nodes[WN_MAX];wide_value values[WV_MAX];
} wide_function;
static bool wb_has(wide_bits b,int n) { return b.w[n/64]&(UINT64_C(1)<<(n%64)); }
static void wb_set(wide_bits *b,int n) { b->w[n/64]|=UINT64_C(1)<<(n%64); }
static bool wb_equal(wide_bits a,wide_bits b) { return !memcmp(&a,&b,sizeof a); }
static wide_bits wb_or(wide_bits a,wide_bits b) { for(int w=0;w<WB_WORDS;++w)a.w[w]|=b.w[w];return a; }
static wide_bits wb_minus(wide_bits a,wide_bits b) { for(int w=0;w<WB_WORDS;++w)a.w[w]&=~b.w[w];return a; }
#include "../wide_liveness.c.inc"

static void reference(wide_function *f)
{
  bool changed;
  do {
    changed=false;
    for(int i=f->nn;i-->0;) {
      wide_node *n=&f->nodes[i];wide_bits out={{0}};
      for(int k=0;k<2+n->jumpCount;++k) {
        int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
        if(to>=0)out=wb_or(out,f->nodes[to].in);
      }
      wide_bits in=wb_or(n->use,wb_minus(out,n->def));
      changed|=!wb_equal(in,n->in)||!wb_equal(out,n->out);n->in=in;n->out=out;
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    for(int a=0;a<f->nv;++a)for(int b=0;b<a;++b)
      if((wb_has(n->in,a)&&wb_has(n->in,b)) ||
         (wb_has(n->def,a)&&wb_has(n->out,b)) ||
         (wb_has(n->def,b)&&wb_has(n->out,a))) {
        wb_set(&f->values[a].edges,b);wb_set(&f->values[b].edges,a);
      }
  }
}

static uint32_t state=0x251cafe;
static unsigned random32(void) { state=state*1664525u+1013904223u;return state; }
int main(void)
{
  const int sizes[]={0,1,63,64,65,127,128,255,256,511,512,513,1023,1024};
  unsigned cases=0;
  wide_function *a=malloc(sizeof *a),*b=malloc(sizeof *b);assert(a&&b);
  for(unsigned size=0;size<sizeof sizes/sizeof *sizes;++size)for(int shape=0;shape<6;++shape) {
    memset(a,0,sizeof *a);a->nv=sizes[size];a->nn=WN_MAX;
    for(int i=0;i<a->nn;++i) {
      wide_node *n=&a->nodes[i];n->succ[0]=i+1<a->nn?i+1:-1;n->succ[1]=-1;
      if(shape&1)n->succ[1]=(int)(random32()%a->nn);
      if(shape>=2) {
        n->jumpStart=i*3;n->jumpCount=3;
        a->jumpSuccessors[i*3]=(int)(random32()%a->nn);
        a->jumpSuccessors[i*3+1]=a->jumpSuccessors[i*3];
        a->jumpSuccessors[i*3+2]=(int)(random32()%a->nn);
      }
      for(int v=0;v<a->nv;++v) {
        unsigned r=random32();
        if(r%(shape>=4?3:79)==0)wb_set(&n->use,v);
        if((r>>8)%(shape>=4?5:37)==0)wb_set(&n->def,v);
      }
      /* Exercise the highest bit even when randomly generated sets are sparse. */
      if(a->nv)wb_set(i==a->nn-1?&n->use:&n->def,a->nv-1);
    }
    /* Existing interference survives recomputation, including unrelated edges. */
    if(a->nv>1) {wb_set(&a->values[0].edges,a->nv-1);wb_set(&a->values[a->nv-1].edges,0);}
    memcpy(b,a,sizeof *b);wide_liveness(a);reference(b);assert(!memcmp(a,b,sizeof *a));
    wide_liveness(a);assert(!memcmp(a,b,sizeof *a));++cases;
  }
  free(b);free(a);printf("PASS: %u CFGs, 0..1024 values, exact interference and fixed points\n",cases);
  return 0;
}
