"""Compare the production binary32 decomposition algorithms with host libm."""
import argparse
import json
from pathlib import Path
import subprocess


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--gcc', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parent.parent / 'native-float/device/lib'
commands = []


def run(args):
    q = subprocess.run(list(map(str, args)), cwd=a.output, capture_output=True)
    commands.append(dict(arguments=list(map(str, args)), exitCode=q.returncode))
    assert q.returncode == 0, (args, q.stdout, q.stderr)
    return q.stdout.decode()


for name in ('floorf', 'ceilf', 'modff'):
    run([a.gcc.resolve(), '-O2', '-std=c99', '-D_FLOAT_FUNC_REENTRANT=',
         '-D'+name+'=mx_'+name, '-c', source/(name+'.c'), '-o', name+'.o'])
(a.output/'check.c').write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <math.h>
float mx_floorf(float); float mx_ceilf(float); float mx_modff(float,float *);
typedef union { float f; uint32_t u; } bits;
static unsigned count;
static int check(uint32_t raw)
{
    bits x, got, want, integer, ref_integer;
    unsigned op;
    x.u=raw;
    for(op=0;op<3;++op) {
        if(op==0) {got.f=mx_floorf(x.f);want.f=floorf(x.f);}
        else if(op==1) {got.f=mx_ceilf(x.f);want.f=ceilf(x.f);}
        else {got.f=mx_modff(x.f,&integer.f);want.f=modff(x.f,&ref_integer.f);}
        /* Some Windows libm modff versions lose the sign of exact zero.
           ISO C requires both parts to have the sign of the argument. */
        if(op==2 && !(want.u&0x7fffffffU))want.u=raw&0x80000000U;
        /* The library transports NaN representations without quieting them. */
        if((raw&0x7fffffffU)>0x7f800000U) {
            want.u=raw;ref_integer.u=raw;
        }
        if(got.u!=want.u || (op==2 && integer.u!=ref_integer.u)) {
            printf("FAIL input=%08x op=%u got=%08x want=%08x integer=%08x ref=%08x\n",
                   raw,op,got.u,want.u,integer.u,ref_integer.u);
            return 1;
        }
        ++count;
    }
    return 0;
}
int main(void)
{
    uint32_t state=0x13579bdfU, sign, e, m, i;
    static const uint32_t tails[]={0,1,2,0x3fffff,0x400000,0x7ffffe,0x7fffff};
    _Static_assert(sizeof(unsigned long)==4,"production unsigned long must be 32-bit");
    for(sign=0;sign<2;++sign) for(e=0;e<256;++e) for(m=0;m<7;++m)
        if(check((sign<<31)|(e<<23)|tails[m]))return 1;
    for(i=0;i<1000000;++i) {
        state=state*1664525U+1013904223U;
        if(check(state))return 1;
    }
    printf("PASS %u exact representation comparisons\n",count);
    return 0;
}
''')
run([a.gcc.resolve(), '-O2', '-fno-builtin', 'check.c', 'floorf.o', 'ceilf.o',
     'modff.o', '-o', 'check.exe', '-lm'])
result = run([a.output.resolve()/'check.exe'])
(a.output/'result.json').write_text(json.dumps(dict(status='PASS', result=result.strip(),
    commands=commands), indent=2)+'\n')
print(result, end='')
