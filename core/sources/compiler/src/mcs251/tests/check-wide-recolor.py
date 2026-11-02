"""Production recoloring invariants; independent byte-interval allocation checker."""
from pathlib import Path
import argparse,hashlib,json,subprocess,re
ap=argparse.ArgumentParser();ap.add_argument('--gcc',required=True);ap.add_argument('--output-dir',required=True);a=ap.parse_args()
S=Path(__file__).resolve().parents[1];D=Path(a.output_dir).resolve();D.mkdir(parents=True,exist_ok=False)
def function(text,name):
 at=re.search(r'\n'+re.escape(name)+r'\s*\(',text).start()+1;start=text.rfind('static ',0,at);brace=text.index('{',at);level=1;i=brace+1
 while level:
  if text[i]=='{':level+=1
  if text[i]=='}':level-=1
  i+=1
 return text[start:i]+'\n'
s=(S/'wide.c').read_text('utf8');r=(S/'wide_recolor.c.inc').read_text('utf8')
header=r"""
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "register_facts.h"
#define WV_MAX 128
#define WA_FAIL(x) 0
typedef struct {uint64_t w[2];} wide_bits;
typedef struct {int width,reg,slot,pin;wide_bits edges;} wide_value;
typedef struct {int slot,size;} wide_object;
typedef struct {int nv,no,frame;bool allocationInvalid;wide_value values[WV_MAX];wide_object objects[32];} wide_function;
static bool wb_has(wide_bits b,int n){return n>=0&&(b.w[n/64]&(UINT64_C(1)<<(n%64)));}
static void wb_set(wide_bits*b,int n){b->w[n/64]|=UINT64_C(1)<<(n%64);}
static uint32_t wide_mask(int r,int w){return mcs251RegisterBytes(r,w);}
"""
body=''.join(function(s,x) for x in ('wide_available','wide_register_choices','wide_layout_spills','wide_validate_allocation'))+function(r,'wide_recolor_place')
tests=r"""
static uint32_t rng=0x51c251u;
static unsigned rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static bool overlap(int a,int aw,int b,int bw){return a>=0&&b>=0&&a<b+bw&&b<a+aw;}
static bool legal(int r,int w){
 if(r<0)return true;
 if(w==1)return r<16&&r!=10&&r!=11;
 if(w==2)return r%2==0&&r<=30&&r!=10&&r!=16&&r!=18&&r!=20&&r!=22;
 return w==4&&(r==0||r==4||r==12||r==24||r==28);
}
static void valid(const wide_function*f,const wide_function*old){
 assert(!f->allocationInvalid);
 for(int i=0;i<f->nv;++i){const wide_value*v=f->values+i;
  assert(legal(v->reg,v->width));assert(v->pin<0||v->pin==v->reg);
  assert(old->values[i].reg<0||v->reg>=0);
  assert(v->width==old->values[i].width&&!memcmp(&v->edges,&old->values[i].edges,sizeof v->edges));
  for(int j=0;j<i;++j)if(wb_has(v->edges,j))assert(!overlap(v->reg,v->width,f->values[j].reg,f->values[j].width));
 }
}
int main(void){
 unsigned tries=0,success=0,fail=0,pinCases=0,budgetStops=0;
 for(int graph=0;graph<1600;++graph){
  wide_function f={0};f.nv=4+rnd()%61;f.no=2;f.objects[0]=(wide_object){0,3};f.objects[1]=(wide_object){3,7};f.frame=10;
  for(int i=0;i<f.nv;++i){wide_value*v=f.values+i;v->width=1<<(rnd()%3);v->reg=v->slot=v->pin=-1;
   for(int j=0;j<i;++j)if(rnd()%100<35){wb_set(&v->edges,j);wb_set(&f.values[j].edges,i);}
   int count;const int*choices=wide_register_choices(v->width,&count);int first=rnd()%count;
   if(rnd()%5)for(int k=0;k<count;++k)if(wide_available(&f,i,choices[(first+k)%count])){v->reg=choices[(first+k)%count];break;}
   if(v->reg>=0&&rnd()%7==0){v->pin=v->reg;++pinCases;}
  }
  valid(&f,&f);
  /* 已分配的值同样需要覆盖：无 spill 的字节访问布局搜索也会调用
   * 同一放置器；冲突、pin、预算耗尽和失败回滚必须遵守相同规则。 */
  for(int v=0;v<f.nv;++v){
   int count;const int*choices=wide_register_choices(f.values[v].width,&count);
   for(int k=0;k<count;++k){
    wide_function a=f,b=f;unsigned budget=tries%3==0?1:1024,bb=budget;wide_bits active={{0}};
    bool ok=wide_recolor_place(&a,v,choices[k],3,active,&budget);
    bool twice=wide_recolor_place(&b,v,choices[k],3,active,&bb);
    assert(ok==twice&&budget==bb&&!memcmp(&a,&b,sizeof a));++tries;if(!budget)++budgetStops;
    if(!ok){++fail;assert(!memcmp(&a,&f,sizeof a));continue;}
    ++success;assert(a.values[v].reg==choices[k]);valid(&a,&f);
    for(int j=0;j<a.nv;++j)a.values[j].slot=-1;
    wide_layout_spills(&a);assert(wide_validate_allocation(&a));
    for(int i=0;i<a.nv;++i)if(a.values[i].reg<0){
     const wide_value*x=a.values+i;assert(x->slot>=10&&x->slot+x->width<=a.frame);
     for(int j=0;j<i;++j)if(a.values[j].reg<0&&wb_has(x->edges,j))assert(!overlap(x->slot,x->width,a.values[j].slot,a.values[j].width));
    }
    assert(a.objects[0].slot==0&&a.objects[1].slot==3);
   }
  }
 }
 assert(success>100&&fail>100&&pinCases>0&&budgetStops>0);
 printf("{\"graphs\":1600,\"attempts\":%u,\"successes\":%u,\"rollbackFailures\":%u,\"pinValues\":%u,\"budgetStops\":%u,\"deterministic\":true}\n",tries,success,fail,pinCases,budgetStops);
 return 0;
}
"""
p=D/'recolor.c';p.write_text(header+body+tests,encoding='utf8')
cmd=[a.gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(S),str(p),'-o',str(D/'recolor.exe')]
q=subprocess.run(cmd,capture_output=True);(D/'compile.stdout.log').write_bytes(q.stdout);(D/'compile.stderr.log').write_bytes(q.stderr);assert q.returncode==0,q.stderr.decode()
q=subprocess.run([str(D/'recolor.exe')],capture_output=True,timeout=60);(D/'run.stdout.log').write_bytes(q.stdout);(D/'run.stderr.log').write_bytes(q.stderr);assert q.returncode==0,q.stderr.decode()
j=json.loads(q.stdout);j.update(passed=True,command=cmd,productionInputs=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in (S/'wide.c',S/'wide_recolor.c.inc',S/'register_facts.h')]);(D/'result.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(j))
