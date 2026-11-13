"""Compare the production binary32 decimal kernel with host printf."""
from pathlib import Path
import argparse
import json
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--gcc', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
kernel = Path(__file__).resolve().parents[2]/'core/build/qualified/FloatFormatKernel.c.template'
source = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
static char actual[300]; static unsigned used;
static int emit(int c) { actual[used++]=(char)c; return c; }
'''+kernel.read_text().replace('putchar(', 'emit(')+r'''
static unsigned checked;
static int check(uint32_t raw, unsigned precision, unsigned width, unsigned flags)
{
    union { uint32_t u; float f; } x;
    char format[32], expected[300];
    int got, want;
    x.u=raw; used=0;
    got=mx_float(raw,precision,width,flags); actual[used]=0;
    sprintf(format,"%%%s%u.%u%c",(flags&4)?"0":"",width,precision,(flags&2)?'F':'f');
    want=snprintf(expected,sizeof(expected),format,(double)x.f);
    /* Host CRTs differ on nonfinite zero padding; check C's space padding. */
    if((raw&0x7fffffffU)>=0x7f800000U) {
        const char *word=(raw&0x7fffffU)?((flags&2)?"NAN":"nan"):((flags&2)?"INF":"inf");
        char signed_word[5]; sprintf(signed_word,"%s%s",(raw>>31)?"-":"",word);
        want=snprintf(expected,sizeof(expected),"%*s",width,signed_word);
    }
    if(got!=want || used!=(unsigned)want || strcmp(actual,expected)) {
        fprintf(stderr,"%08x %s: [%s] (%d) != [%s] (%d)\n",raw,format,actual,got,expected,want);
        return 0;
    }
    ++checked; return 1;
}
int main(void)
{
    uint32_t raw=0x12345678U;
    unsigned e,s,m,p,i;
    const uint32_t mant[]={0,1,0x3fffff,0x400000,0x7ffffe,0x7fffff};
    for(e=0;e<256;++e) for(s=0;s<2;++s) for(m=0;m<6;++m) for(p=0;p<10;++p)
        if(!check((s<<31)|(e<<23)|mant[m],p,(p&1)?60:0,(p&2)?6:0)) return 1;
    for(i=0;i<50000;++i) {
        raw=raw*1664525U+1013904223U;
        if(!check(raw,i%10,(i%4)?0:255,(i%3)?4:2)) return 1;
    }
    printf("%u\n",checked); return 0;
}
'''
(a.output/'check.c').write_text(source, encoding='utf-8', newline='\n')
subprocess.run([str(a.gcc), '-O2', '-std=c99', 'check.c', '-o', 'check.exe'], cwd=a.output, check=True)
q = subprocess.run([str((a.output/'check.exe').resolve())], capture_output=True, text=True)
(a.output/'check.log').write_text(q.stdout+q.stderr)
if q.returncode:
    raise RuntimeError(q.stderr)
result = dict(status='PASS', cases=int(q.stdout), precision=list(range(10)),
              allExponents=True, bothSigns=True, hardwareExecuted=False)
(a.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
print(result)
