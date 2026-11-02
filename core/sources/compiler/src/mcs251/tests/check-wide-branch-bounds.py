"""Compile the actual pure relaxation helper with a host harness; test boundaries."""
from pathlib import Path
import argparse,subprocess,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--cc',type=Path,required=True);p.add_argument('--source-dir',type=Path,default=Path(__file__).resolve().parent.parent);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args();d=a.output_dir;d.mkdir(parents=True,exist_ok=False)
source=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#define WN_MAX 512
#define wassert assert
struct dbuf_s {char *p;size_t n,cap;};
static void dbuf_init(struct dbuf_s *b,size_t n){b->n=0;b->cap=n+1024;b->p=calloc(1,b->cap);assert(b->p);}
static size_t dbuf_get_length(struct dbuf_s *b){return b->n;}
static const char *dbuf_get_buf(struct dbuf_s *b){return b->p;}
static void dbuf_append(struct dbuf_s *b,const char *s,size_t n){assert(b->n+n<b->cap);memcpy(b->p+b->n,s,n);b->n+=n;b->p[b->n]=0;}
static void dbuf_printf(struct dbuf_s *b,const char *s,...){char t[512];va_list v;va_start(v,s);int n=vsnprintf(t,sizeof t,s,v);va_end(v);assert(n>=0 && n<(int)sizeof t);dbuf_append(b,t,n);}
static void dbuf_destroy(struct dbuf_s *b){free(b->p);}
typedef struct{int label;size_t text;unsigned upper,barrier;}wide_label_site;
typedef struct{int target;const char *condition;size_t begin,end;unsigned upperBegin,upperEnd,barrier,weight;}wide_branch_site;
typedef struct{wide_label_site labels[WN_MAX*4];wide_branch_site branches[WN_MAX];unsigned labelCount,branchCount;bool relaxDisabled;unsigned emitInstructions,emitCost;struct dbuf_s output;}wide_function;
static struct {const char *rname;} fn={"test"},*currFunc=&fn;
#include "wide_branch.c.inc"
static unsigned checks;
static void test(unsigned distance,bool back,bool conditional,int special){
 wide_function *f=calloc(1,sizeof *f);assert(f);dbuf_init(&f->output,64);dbuf_append(&f->output,"T:\nLONG\nEND\n",12);
 f->emitInstructions=10;f->emitCost=50;f->branchCount=1;f->labelCount=1;
 f->branches[0]=(wide_branch_site){7,conditional?"jsl":NULL,3,8,back?distance:10,back?distance+12:22,0,5};
 f->labels[0]=(wide_label_site){7,back?0:8,back?0:22+distance,0};
 if(special==1)f->labels[0].barrier=1;
 if(special==2)f->relaxDisabled=true;
 if(special==3)f->labels[0].label=8;
 if(special==4){f->labels[1]=f->labels[0];f->labelCount=2;}
 if(special==5)f->labels[0].text=5;
 bool expected=!special && (back?distance+(conditional?3:2)<=128:distance<=127);
 wrelax_branches(f);assert((strstr(f->output.p,"wide short branch")!=NULL)==expected);
 assert(f->emitInstructions==(unsigned)(10-(expected&&conditional)));assert(f->emitCost==(unsigned)(50-(expected&&conditional?5:0)));
 if(expected){assert(strstr(f->output.p,conditional?"\tjsl\t__wm_test_7":"\tsjmp\t__wm_test_7"));assert(!strstr(f->output.p,"LONG"));assert(strstr(f->output.p,"END"));}
 else assert(!strcmp(f->output.p,"T:\nLONG\nEND\n"));
 dbuf_destroy(&f->output);free(f);++checks;
}
int main(void){
 for(unsigned d=0;d<=512;++d)for(int b=0;b<2;++b)for(int c=0;c<2;++c)test(d,b,c,0);
 for(int s=1;s<=5;++s)for(int b=0;b<2;++b)for(int c=0;c<2;++c)test(1,b,c,s);
 assert(wbranch_instruction_bound("mov\tr7,@spx-0x01ff",21)==6);
 assert(wbranch_instruction_bound("je\ttarget",9)==6);
 assert(!wbranch_instruction_bound(".align 256",10));assert(!wbranch_instruction_bound("unknown r0",10));assert(!wbranch_instruction_bound("movx? r0",8));
 printf("PASS %u range/barrier/disabled/missing/duplicate/internal-target cases; exact helper source\n",checks);return 0;
}
'''
(d/'harness.c').write_text(source);(d/'wide_branch.c.inc').write_bytes((a.source_dir/'wide_branch.c.inc').read_bytes())
args=[str(a.cc.resolve()),'-std=c17','-O2','-Wall','-Wextra','harness.c','-o','harness.exe'];q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
q=subprocess.run([str((d/'harness.exe').resolve())],cwd=d,capture_output=True);(d/'run.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,(q.stdout,q.stderr)
(d/'results.json').write_text(json.dumps(dict(passed=True,cases=2072,actualHelperSha256=hashlib.sha256((d/'wide_branch.c.inc').read_bytes()).hexdigest(),compiler=str(a.cc)),indent=2));print(q.stdout.decode())
