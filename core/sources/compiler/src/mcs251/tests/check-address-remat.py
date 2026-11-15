"""Compile address recipes in four modes and check sampled-operand CFG lifetime."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
for key in ('sdcc','baseline','support','output','cc'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env.update(SDCC_HOME=str(a.support),MX251_WIDE_VALUE_LIMIT='1024',MX251_WIDE_NODE_LIMIT='2048')
env['PATH']=str(a.support/'bin')+os.pathsep+env['PATH']
rows=[]
for model in ('small','large'):
 for opt in ('speed','size'):
  texts={}
  for variant,compiler in [('before',a.baseline),('after',a.sdcc),('disabled',a.sdcc)]:
   d=a.output/(model+'-'+opt)/variant;d.mkdir(parents=True)
   for name in ('address-remat.h','address-remat-access.c'):shutil.copy2(tests/name,d/name)
   en=dict(env)
   if variant=='disabled':en['MX251_ADDRESS_REMATERIALIZATION']='0'
   args=['-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto','--mcs251-wide-ordinary',
         '--mcs251-machine-ir','--mcs251-function-metadata','--mcs251-flat-code-reads','-c','address-remat-access.c']
   q=subprocess.run([str(compiler),*args],cwd=d,env=en,capture_output=True)
   (d/'compiler.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
   texts[variant]=(d/'address-remat-access.asm').read_text()
   rows.append(dict(model=model,opt=opt,variant=variant,command=[str(compiler),*args]))
  def instructions(text):return [l.split(';',1)[0].strip() for l in text.splitlines() if l.split(';',1)[0].strip()]
  assert instructions(texts['before'])==instructions(texts['disabled']),(model,opt,'disabled differs')
  selected=re.search(r'^; wide address-remat choice function=remat_fields .* selected=1 .*old-bytes=(\d+) new-bytes=(\d+).*old-frame=(\d+) new-frame=(\d+)',texts['after'],re.M)
  assert selected and int(selected[2])<int(selected[1]) and int(selected[4])<int(selected[3]),(model,opt)

# Compile the production CFG helper itself. The oracle executes sampled
# operand generations along paths, independently of the helper's worklist.
source=(tests.parent/'wide_address_remat.c.inc').read_text()
helper=source[source.index('static void\nwide_address_recipe_stale'):source.index('static bool\nwide_address_recipe_memory')]
driver=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
enum { WN_MAX=64, WV_MAX=8 };
typedef struct { unsigned bits; } wide_bits;
typedef struct { int succ[2],jumpCount,jumpStart;wide_bits def; } wide_node;
typedef struct { int nn,nv;wide_node nodes[WN_MAX];int jumpSuccessors[WN_MAX*3]; } wide_function;
static int wb_has(wide_bits b,int v) {return v>=0 && (b.bits&(1u<<v));}
'''+helper+r'''
static uint32_t randomState=0x8251;
static unsigned next(void) {randomState=randomState*1664525u+1013904223u;return randomState;}
static void reset(wide_function *f,int n) {
 memset(f,0,sizeof *f);f->nn=n;f->nv=2;
 for(int i=0;i<n;++i) {f->nodes[i].succ[0]=i+1<n?i+1:-1;f->nodes[i].succ[1]=-1;}
}
int main(void) {
 wide_function f;unsigned char stale[WN_MAX];wide_bits inputs={3};unsigned checks=0;
 reset(&f,5);f.nodes[0].succ[1]=2;f.nodes[1].def.bits=1;
 memset(stale,0,sizeof stale);wide_address_recipe_stale(&f,0,inputs,stale);assert(stale[3]);
 reset(&f,4);f.nodes[2].def.bits=2;f.nodes[3].succ[0]=1;
 memset(stale,0,sizeof stale);wide_address_recipe_stale(&f,0,inputs,stale);assert(stale[1]);
 reset(&f,4);f.nodes[2].def.bits=2;f.nodes[3].succ[0]=0;
 memset(stale,0,sizeof stale);wide_address_recipe_stale(&f,0,inputs,stale);assert(!stale[1]);
 reset(&f,5);f.nodes[1].succ[0]=-1;f.nodes[1].jumpCount=2;f.jumpSuccessors[0]=2;f.jumpSuccessors[1]=3;f.nodes[2].def.bits=1;
 memset(stale,0,sizeof stale);wide_address_recipe_stale(&f,0,inputs,stale);assert(stale[4]);
 for(unsigned graph=0;graph<256;++graph) {
  reset(&f,8+next()%24);
  for(int i=1;i<f.nn;++i) {
   wide_node *n=&f.nodes[i];n->def.bits=(next()>>24)&3;
   n->succ[0]=(int)(next()%(f.nn+1))-1;n->succ[1]=(int)(next()%(f.nn+1))-1;
   if((next()&3)==0) {n->jumpCount=3;n->jumpStart=i*3;
    for(int k=0;k<3;++k)f.jumpSuccessors[n->jumpStart+k]=next()%f.nn;}
  }
  memset(stale,0,sizeof stale);wide_address_recipe_stale(&f,0,inputs,stale);
  for(unsigned walk=0;walk<64;++walk) {
   int at=0;unsigned generation[2]={0},sample[2]={0};
   for(unsigned step=0;step<128 && at>=0;++step) {
    wide_node *n=&f.nodes[at];
    if(!at)memcpy(sample,generation,sizeof sample);
    else if(!stale[at]) {assert(sample[0]==generation[0] && sample[1]==generation[1]);++checks;}
    for(int v=0;v<2;++v)if(wb_has(n->def,v))++generation[v];
    unsigned edge=next()%(2+n->jumpCount);
    at=edge<2?n->succ[edge]:f.jumpSuccessors[n->jumpStart+edge-2];
   }
  }
 }
 printf("PASS 4 lifetime cases, 256 CFGs, 16384 walks, %u clean-use observations\n",checks);return 0;
}
'''
(a.output/'host.c').write_text(driver,newline='\n')
exe=a.output/'host.exe'
q=subprocess.run([str(a.cc),'-std=c99','-O2',str(a.output/'host.c'),'-o',str(exe)],capture_output=True)
assert q.returncode==0,q.stderr
q=subprocess.run([str(exe)],capture_output=True);assert q.returncode==0,q.stderr
(a.output/'host.log').write_bytes(q.stdout+q.stderr)
result=dict(status='PASS',configurations=4,compilerCommands=rows,host=q.stdout.decode().strip(),
 compilerSha256=hashlib.sha256(a.sdcc.read_bytes()).hexdigest(),parentSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest())
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['host']);print('PASS four modes, parent/disabled instruction equality and structure-frame elimination')
