"""Execute the production MIR scheduler against an independent memory interpreter."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
p=argparse.ArgumentParser();p.add_argument('--gcc',required=True);p.add_argument('--output-dir',required=True);a=p.parse_args()
S=Path(__file__).resolve().parents[1];D=Path(a.output_dir).resolve();D.mkdir(parents=True,exist_ok=False)
header=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define WV_MAX 128
#define WN_MAX 512
enum {WM_LOAD=65536,WM_STORE,WM_ADDRESS,WM_UMULW,WM_NOP,CAST=380,
 RETURN=1001,ENDFUNCTION,GOTO,JUMPTABLE,LABEL,IFX,CALL,BITWISEAND=371};
typedef struct {uint64_t w[2];} wide_bits;
typedef struct {int value,width;uint32_t literal;bool floating;} wide_operand;
typedef struct {int width,pin;bool pointer,codePointer,symbolSnapshot,floating;} wide_value;
typedef struct {int op,result,width,addressObject,addressOp,args,stackArgs,succ[2],jumpCount;
 wide_operand left,right,addressIndex;bool byteAccess,codeAccess,pointerAccess,floating;
 wide_bits use,def,out;} wide_node;
typedef struct {int nv,nn,nj;void*diagnostics;wide_node nodes[WN_MAX];wide_value values[WV_MAX];int jumpSuccessors[2048];} wide_function;
static bool wb_has(wide_bits x,int i){return i>=0 && (x.w[i/64]&(UINT64_C(1)<<(i%64)));}
static void dbuf_printf(void*d,const char*fmt,...){(void)d;(void)fmt;}
static void wide_reset_allocation(wide_function*f){(void)f;}
'''
s=(S/'wide.c').read_text('utf8');start=s.index('static bool\nwide_sink_fallthrough');end=s.index('\n}',start)+2
header+=s[start:end]+'\n#include "wide_load_schedule.c.inc"\n'
tests=r'''
static void bit(wide_bits*b,int v){if(v>=0)b->w[v/64]|=UINT64_C(1)<<(v%64);}
static wide_node node(int op,int result,int left,int right,int width){
 wide_node n={0};n.op=op;n.result=result;n.left.value=left;n.left.width=4;
 n.right.value=right;n.width=width;n.addressIndex.value=-1;n.addressObject=-1;
 n.succ[0]=n.succ[1]=-1;bit(&n.use,left);bit(&n.use,right);bit(&n.def,result);return n;
}
static void live(wide_function*f){
 uint64_t in[WN_MAX]={0},out[WN_MAX]={0};bool changed;
 do{changed=false;for(int i=f->nn-1;i>=0;--i){wide_node*n=&f->nodes[i];uint64_t o=0;
  for(int k=0;k<2;++k)if(n->succ[k]>=0)o|=in[n->succ[k]];
  uint64_t v=n->use.w[0]|(o&~n->def.w[0]);changed|=in[i]!=v||out[i]!=o;in[i]=v;out[i]=o;
 }}while(changed);
 for(int i=0;i<f->nn;++i)f->nodes[i].out.w[0]=out[i];
}
static wide_function make(int w1,int w2){
 wide_function f={0};f.nv=7;f.nn=7;
 for(int v=0;v<f.nv;++v){f.values[v].pin=-1;f.values[v].width=4;}
 f.values[0].pointer=f.values[1].pointer=f.values[2].pointer=true;
 f.values[3].width=w1;f.values[4].width=w2;
 f.nodes[0]=node(LABEL,-1,-1,-1,0);
 f.nodes[1]=node('+',2,0,-1,4);f.nodes[1].right.literal=1;
 f.nodes[2]=node(WM_LOAD,3,2,-1,w1);
 f.nodes[3]=node(WM_LOAD,4,1,-1,w2);
 f.nodes[4]=node('+',5,3,4,4);
 f.nodes[5]=node(WM_STORE,-1,2,5,w1);
 f.nodes[6]=node(RETURN,-1,5,-1,4);
 for(int i=0;i<f.nn-1;++i)f.nodes[i].succ[0]=i+1;
 live(&f);return f;
}
typedef struct {uint32_t v[WV_MAX];unsigned char mem[64];unsigned reads[64],writes[64],steps;} state;
static uint32_t operand(wide_operand o,state*s){return o.value<0?o.literal:s->v[o.value];}
static state run(wide_function*f,int x,int y,unsigned seed){
 state s={0};s.v[0]=x;s.v[1]=y;for(unsigned i=0;i<64;++i)s.mem[i]=(unsigned char)(i*29+seed*37);
 int at=0;while(at>=0){assert(++s.steps<100);wide_node*n=&f->nodes[at];uint32_t l=operand(n->left,&s),r=operand(n->right,&s);
  if(n->op=='+')s.v[n->result]=l+r;
  else if(n->op==WM_LOAD){assert(l+n->width<=64);uint32_t v=0;
   for(int k=0;k<n->width;++k){v=(v<<8)|s.mem[l+k];++s.reads[l+k];}s.v[n->result]=v;
  }else if(n->op==WM_STORE){assert(l+n->width<=64);
   for(int k=0;k<n->width;++k){s.mem[l+k]=(unsigned char)(r>>(8*(n->width-1-k)));++s.writes[l+k];}
  }else if(n->op==RETURN)break;
  else assert(n->op==LABEL||n->op==WM_NOP);
  at=n->succ[0];
 }return s;
}
int main(void){unsigned executions=0,negative=0;
 for(int w1=1;w1<=4;w1*=2)for(int w2=1;w2<=4;w2*=2){
  wide_function parent=make(w1,w2),trial=parent;
  assert(wide_schedule_last_loads(&trial)==1);
  assert(trial.nodes[1].result==4&&trial.nodes[2].result==2&&trial.nodes[3].result==3);
  live(&trial);assert(wide_schedule_last_loads(&trial)==0);
  for(int x=0;x<12;++x)for(int y=0;y<16;++y)for(int seed=0;seed<9;++seed){
   state a=run(&parent,x,y,seed),b=run(&trial,x,y,seed);assert(!memcmp(&a,&b,sizeof a));++executions;
  }
 }
 /* 每项单独建立拒绝条件，不能靠关闭 pass 通过。 */
 for(int c=0;c<24;++c){wide_function f=make(2,1);
  switch(c){
  case 0:f.nodes[3].byteAccess=true;break;
  case 1:f.nodes[2].byteAccess=true;break;
  case 2:f.nodes[3].codeAccess=true;break;
  case 3:f.nodes[2].codeAccess=true;break;
  case 4:f.nodes[3].pointerAccess=true;break;
  case 5:f.nodes[3].addressObject=0;break;
  case 6:f.nodes[3].addressOp='+';break;
  case 7:f.nodes[2].op=WM_STORE;break;
  case 8:f.nodes[2].op=CALL;break;
  case 9:f.nodes[1].op=LABEL;f.nodes[2].op=WM_STORE;break;
  case 10:f.nodes[3].out.w[0]|=2;break;
  case 11:f.nodes[2].def.w[0]|=2;break;
  case 12:f.nodes[2].use.w[0]|=16;break;
  case 13:f.values[4].pin=9;break;
  case 14:f.values[4].pointer=true;break;
  case 15:f.values[1].codePointer=true;break;
  case 16:f.nodes[3].use.w[0]|=1;break;
  case 17:f.nodes[2].succ[1]=3;break;
  case 18:f.nj=1;f.jumpSuccessors[0]=2;break;
  case 19:f.nodes[0].succ[1]=2;break;
  case 20:f.nodes[2].floating=true;break;
  case 21:f.nodes[2].op='/';break;
  case 22:f.nodes[2].op=CAST;f.nodes[2].right.floating=true;break;
  case 23:f.nodes[3].stackArgs=1;break;
  }
  wide_function old=f;assert(wide_schedule_last_loads(&f)==0);assert(!memcmp(&old,&f,sizeof f));++negative;
 }
 printf("{\"memoryExecutions\":%u,\"rejectionContracts\":%u,\"widthPairs\":9,\"readWriteCountsAndValuesIdentical\":true,\"secondPassIdempotent\":true}\n",executions,negative);
 return 0;
}
'''
(D/'scheduler.c').write_text(header+tests,encoding='utf8')
cmd=[a.gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(S),str(D/'scheduler.c'),'-o',str(D/'scheduler.exe')]
q=subprocess.run(cmd,capture_output=True);(D/'compile.stdout.log').write_bytes(q.stdout);(D/'compile.stderr.log').write_bytes(q.stderr);assert q.returncode==0,q.stderr.decode()
q=subprocess.run([str(D/'scheduler.exe')],capture_output=True,timeout=60);(D/'run.stdout.log').write_bytes(q.stdout);(D/'run.stderr.log').write_bytes(q.stderr);assert q.returncode==0,q.stderr.decode()
result=json.loads(q.stdout);result['source']={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in (S/'wide.c',S/'wide_load_schedule.c.inc',Path(__file__))};result['compileCommand']=cmd
(D/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(q.stdout.decode())
