"""Execute the actual MIR pass against an independent load/copy interpreter.

This supplements emitted-code/board tests; it is not target execution.
Barriers are adversarial sequential boundaries, not a CFG emulator.
"""
from pathlib import Path
import argparse,subprocess,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--cc',required=True,help='Explicit host C compiler path');a=p.parse_args()
T=Path(__file__).resolve().parent;source=(T.parent/'wide.c').read_text();D=a.output_dir;D.mkdir(exist_ok=False)
types=source[source.index('enum { WV_MAX'):source.index('static bool wb_equal')]
start=source.index('static void\nwide_reuse_parameter_loads');end=source.index('\nstatic void\nwide_strength_reduce',start);actual=source[start:end]
pre='''#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void symbol; typedef void iCode; struct dbuf_s {int dummy;};
enum {SEND=256,CAST,BITWISEAND,LEFT_OP,RIGHT_OP,GETABIT,GETBYTE,GETWORD,LE_OP,GE_OP,EQ_OP,NE_OP,CALL,LABEL,IFX,GOTO,JUMPTABLE,FUNCTION,RETURN,UNKNOWN};
static bool mcs251WideOrdinaryEnabled;
'''
harness=r'''
static wide_function before,after;
static uint32_t seed=0x2518401;
static unsigned checks, hits, barriers;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d case %u\n",__LINE__,checks);exit(1);}} while(0)
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint32_t mask(int w){return w==4?UINT32_MAX:((UINT32_C(1)<<(8*w))-1);}
/* Reference evaluation uses bytes and arithmetic independently of pass matching. */
static void evaluate(wide_function *f,const unsigned char *input,uint32_t *v,unsigned *events){
  unsigned char memory[16];memcpy(memory,input,16);memset(v,0,8*sizeof(*v));*events=0;
  for(int i=0;i<f->nn;++i){wide_node *n=&f->nodes[i];uint32_t x=0;
    if(n->byteAccess)++*events;
    if(n->op==WM_PARAM_LOAD){
      for(int j=0;j<n->left.width;++j)x=(x<<8)|memory[n->addressOffset+j];
      if(n->left.sign && n->left.width<4 && (x&(UINT32_C(1)<<(8*n->left.width-1))))x|=~mask(n->left.width);
    }else if(n->op=='=')x=n->right.value>=0?v[n->right.value]:n->right.literal;
    else if(n->op=='^')x=v[n->left.value]^n->right.literal;
    else if(n->op=='+')x=v[n->left.value]+v[n->right.value];
    else { /* Simulate an arbitrary memory change at an opaque boundary. */
      if(n->label)for(int j=0;j<16;++j)memory[j]^=(unsigned char)(j+0x97);
      continue;
    }
    if(n->result>=0)v[n->result]=x&mask(n->width);
  }
}
static void load(int at,int result,int sw,int dw,int sign){
  wide_node *n=&before.nodes[at];n->op=WM_PARAM_LOAD;n->result=result;n->width=dw;n->sign=sign;
  n->left=(wide_operand){.value=-1,.width=sw,.sign=sign};n->addressOffset=0;wb_set(&n->def,result);
  before.values[result].width=dw;before.values[result].sign=sign;
}
static void run_case(int sw,int dw,int sign,int mode,unsigned barrier,unsigned char *memory){
  memset(&before,0,sizeof(before));before.nv=4;before.nn=4;
  load(0,0,sw,dw,sign);load(2,2,sw,dw,sign);
  wide_node *mid=&before.nodes[1],*last=&before.nodes[3];
  mid->op='^';mid->result=1;mid->width=4;mid->left.value=0;mid->right.literal=random32();wb_set(&mid->def,1);wb_set(&mid->use,0);before.values[1].width=4;
  last->op='+';last->result=3;last->width=4;last->left.value=1;last->right.value=2;wb_set(&last->def,3);wb_set(&last->use,1);wb_set(&last->use,2);before.values[3].width=4;
  int expected=1;
  switch(mode){
    case 0:break;
    case 1:mid->op=barrier;mid->result=-1;mid->def=(wide_bits){{0}};mid->label=1;expected=0;break;
    case 2:mid->byteAccess=true;expected=0;break;
    case 3:before.nodes[2].left.sign=!sign;expected=0;break;
    case 4:before.nodes[2].addressOffset=5;expected=0;break;
    case 5:before.nodes[2].width=dw==4?2:4;before.values[2].width=before.nodes[2].width;expected=0;break;
    case 6:before.nodes[2].sign=!sign;before.values[2].sign=!sign;expected=0;break;
    case 7:before.nodes[2].floating=true;before.values[2].floating=true;expected=0;break;
    case 8:before.values[2].pointer=true;expected=0;break;
    case 9:before.values[0].pointer=before.values[2].pointer=true;before.values[2].codePointer=true;expected=0;break;
    case 10:mid->op='=';mid->result=0;mid->right.value=-1;mid->width=dw;mid->def=(wide_bits){{0}};wb_set(&mid->def,0);expected=0;break;
    case 11:before.nodes[2].left.width=sw==4?2:4;expected=0;break;
    case 12:before.nodes[0].floating=before.nodes[2].floating=true;before.values[0].floating=before.values[2].floating=true;break;
    case 13:before.values[0].pointer=before.values[2].pointer=true;break;
    case 14:before.values[0].pointer=before.values[2].pointer=true;before.values[0].codePointer=before.values[2].codePointer=true;break;
    case 15:before.nodes[2].byteAccess=true;expected=0;break;
    case 16:before.nodes[2].result=0;before.nodes[2].def=(wide_bits){{0}};wb_set(&before.nodes[2].def,0);last->right.value=0;expected=0;break;
  }
  after=before;mcs251WideOrdinaryEnabled=false;wide_reuse_parameter_loads(&after);CHECK(!memcmp(&before,&after,sizeof(before)));
  mcs251WideOrdinaryEnabled=true;wide_reuse_parameter_loads(&after);CHECK(after.reusedParameterLoads==expected);
  uint32_t a[8],b[8];unsigned ae,be;evaluate(&before,memory,a,&ae);evaluate(&after,memory,b,&be);CHECK(!memcmp(a,b,sizeof(a)));CHECK(ae==be);
  ++checks;hits+=expected;barriers+=mode==1;
}
int main(void){
  const unsigned ops[]={CALL,WM_PARAM_STORE,WM_STORE,WM_OBJECT_STORE,WM_SYMBOL_STORE,WM_BIT_STORE,WM_LOAD,WM_OBJECT_LOAD,WM_SYMBOL_LOAD,WM_BIT_LOAD,WM_PARAM_ADDRESS,WM_ADDRESS,LABEL,IFX,GOTO,JUMPTABLE,FUNCTION,RETURN,UNKNOWN};
  const int widths[][2]={{1,1},{1,2},{1,4},{2,2},{2,4},{3,4},{4,4}};
  for(unsigned w=0;w<sizeof(widths)/sizeof(*widths);++w)for(int sign=0;sign<2;++sign)for(int sample=0;sample<64;++sample){
    unsigned char memory[16];for(int j=0;j<16;++j)memory[j]=(unsigned char)random32();
    for(int mode=0;mode<=16;++mode){
      if(mode>=12 && mode<=14 && widths[w][1]!=4)continue;
      if(mode==1)for(unsigned k=0;k<sizeof(ops)/sizeof(*ops);++k)run_case(widths[w][0],widths[w][1],sign,mode,ops[k],memory);
      else run_case(widths[w][0],widths[w][1],sign,mode,0,memory);
    }
  }
  printf("{\"passed\":true,\"cases\":%u,\"reuses\":%u,\"barrierCases\":%u,\"opaqueBarrierKinds\":19}\n",checks,hits,barriers);
}
'''
(D/'actual-pass.inc').write_text(actual);(D/'harness.c').write_text(pre+types+actual+harness)
rows=[]
for opt in ['O0','O2']:
    cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-'+opt,'harness.c','-o',opt+'.exe']
    q=subprocess.run(cmd,cwd=D,capture_output=True);(D/(opt+'-compile.log')).write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
    q=subprocess.run([str((D/(opt+'.exe')).resolve())],capture_output=True);(D/(opt+'-run.log')).write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
    rows.append(dict(optimization=opt,**json.loads(q.stdout)))
(D/'results.json').write_text(json.dumps(dict(actualPassSha256=hashlib.sha256(actual.encode()).hexdigest(),rows=rows,scope='Host interpreter of actual pass; not MCU or CFG simulation'),indent=2));print(rows)
