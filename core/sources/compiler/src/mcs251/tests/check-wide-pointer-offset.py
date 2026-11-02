"""Compile the actual offset admission function with a minimal IR fixture."""
from pathlib import Path
import argparse,subprocess,json,re,hashlib
p=argparse.ArgumentParser();p.add_argument('--gcc',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(exist_ok=False)
source=(Path(__file__).resolve().parents[1]/'wide.c').read_text()
fn=re.search(r'static bool\nwide_pointer_offset\(.*?\n\}',source,re.S)[0]
header=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {bool pointer,integral,unsign;unsigned size;} Type;
typedef struct {Type type;bool literal;uint64_t bits;} operand;
typedef struct {int op;operand result,left,right;} iCode;
typedef struct {int value;unsigned width;uint32_t literal;bool sign;} wide_operand;
#define IC_RIGHT(ic) (&(ic)->right)
#define IC_LEFT(ic) (&(ic)->left)
#define IC_RESULT(ic) (&(ic)->result)
#define operandType(o) (&(o)->type)
#define wide_pointer(t) ((t)->pointer)
#define IS_OP_LITERAL(o) ((o)->literal)
#define IS_INTEGRAL(t) ((t)->integral)
#define getSize(t) ((t)->size)
#define getSpec(t) (t)
#define SPEC_USIGN(t) ((t)->unsign)
#define operandLitValueUll(o) ((o)->bits)
'''
# Exact boundary expectations independent of the implementation's comparisons.
cases=[(0,0,True,0,False),(1,0,True,1,False),(64,0,True,64,False),(0x7fffffff,0,True,0x7fffffff,False),
 (0x80000000,0,True,0x80000000,False),(0xffffffff,0,True,0xffffffff,False),(0x100000000,0,False,0,False),
 (0xffffffffffffffff,0,True,0xffffffff,True),(0xffffffff80000000,0,True,0x80000000,True),
 (0xffffffff7fffffff,0,False,0,False),(0x8000000000000000,0,False,0,False),
 (0xffffffff,1,True,0xffffffff,False),(0x100000000,1,False,0,False),
 (0xffffffffffffffff,1,False,0,False),(0x8000000000000000,1,False,0,False)]
body='int main(void){unsigned count=0; iCode c={0}; wide_operand v;\n'
body+='c.result.type.pointer=c.left.type.pointer=true;c.right.type.integral=true;c.right.type.size=8;c.right.literal=true;\n'
for op in ['+','-']:
 for bits,unsigned,ok,literal,sign in cases:
  body+=f"c.op='{op}';c.right.type.unsign={unsigned};c.right.bits=UINT64_C(0x{bits:016x});v=(wide_operand){{.value=42,.width=17,.literal=123,.sign=true}};\n"
  if ok:body+=f'if(!wide_pointer_offset(&c,&v)||v.value!=-1||v.width!=4||v.literal!=UINT32_C(0x{literal:08x})||v.sign!={int(sign)})return 1;\n'
  else:body+='if(wide_pointer_offset(&c,&v)||v.value!=42||v.width!=17||v.literal!=123||!v.sign)return 2;\n'
  body+='++count;\n'
for mutation in ["c.op='*'","c.op='<'","c.result.type.pointer=false","c.left.type.pointer=false","c.right.literal=false","c.right.type.integral=false","c.right.type.size=4"]:
 body+="c.op='+';c.result.type.pointer=c.left.type.pointer=true;c.right.type.integral=true;c.right.type.size=8;c.right.literal=true;c.right.bits=64;c.right.type.unsign=false;"+mutation+';if(wide_pointer_offset(&c,&v))return 3;++count;\n'
body+='printf("%u\\n",count);return 0;}\n'
(a.output_dir/'offset.c').write_text(header+fn+'\n'+body)
rows=[]
for opt in ['O0','O2']:
 exe=(a.output_dir/(opt+'.exe')).resolve()
 q=subprocess.run([str(a.gcc.resolve()),'-std=c11','-Wall','-Wextra','-Werror','-'+opt,str((a.output_dir/'offset.c').resolve()),'-o',str(exe)],capture_output=True)
 (a.output_dir/(opt+'.log')).write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
 q=subprocess.run([str(exe)],capture_output=True);assert not q.returncode and q.stdout.strip()==b'37',q
 rows.append(dict(optimization=opt,checks=37,passed=True))
(a.output_dir/'results.json').write_text(json.dumps(dict(sourceFunctionSha256=hashlib.sha256(fn.encode()).hexdigest(),scope='Actual admission function, stubbed IR accessors; not target execution',rows=rows),indent=2));print('PASS 37 offset admission checks at O0 and O2')
