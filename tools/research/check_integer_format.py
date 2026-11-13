"""Compare generated integer kernels with host printf, including radix subsets."""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('pwsh', 'gcc', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
qualified = Path(__file__).resolve().parents[2]/'core/build/qualified'


def quote(path):
    return "'"+str(path).replace("'", "''")+"'"


script = '''$ErrorActionPreference='Stop'
. QUALIFIED
$kernel=[IO.File]::ReadAllText(KERNEL)
$rows=@()
foreach($mask in 1..7){
 $tokens=@(); foreach($pair in @(@(1,8),@(2,10),@(4,16))){
  if($mask -band $pair[0]){$tokens+=@{radix=$pair[1]}}
 }
 foreach($mode in 0..2){
  $bits=if($mode -eq 0){16}else{32}
  $name='number_'+$mask+'_'+$mode
  $body=New-Mx251IntegerFormatKernel $kernel $tokens $bits -NativeDivision:($mode -eq 2)
  $rows+=@{mask=$mask;bits=$bits;native=($mode -eq 2);name=$name;source=$body.Replace('mx_number',$name).Replace('putchar(','emit(')}
 }
}
ConvertTo-Json -InputObject $rows -Depth 5 | Set-Content -LiteralPath OUTPUT -Encoding utf8
'''.replace('QUALIFIED', quote(qualified/'ExtendedFormat.ps1')).replace(
    'KERNEL', quote(qualified/'IntegerFormatKernel.c.template')).replace(
    'OUTPUT', quote(a.output/'kernels.json'))
(a.output/'generate.ps1').write_text(script, encoding='utf-8')
subprocess.run([str(a.pwsh), '-NoProfile', '-File', str(a.output/'generate.ps1')], check=True)
rows = json.loads((a.output/'kernels.json').read_text(encoding='utf-8-sig'))
source = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
static char actual[300]; static unsigned used, checked;
static int emit(int c) { actual[used++]=(char)c; return c; }
'''+''.join(r['source']+'\n' for r in rows)
source += '''static unsigned number(unsigned which, uint32_t value, unsigned radix, unsigned width, unsigned flags)
{ switch(which) {
'''+''.join(f"case {i}: return {r['name']}(value,radix,width,flags);\n" for i, r in enumerate(rows))+'''
} return 0; }
static int check(unsigned which, unsigned bits, uint32_t value, unsigned radix, unsigned width, unsigned flags)
{
    char expected[300], format[32]; int got,want; int32_t signed_value;
    if(bits==16) value&=65535u;
    signed_value=bits==16?(int16_t)value:(int32_t)value;
    sprintf(format,"%%%s%u%c",(flags&4)?"0":"",width,
        radix==8?'o':radix==16?((flags&2)?'x':'X'):(flags&1)?'d':'u');
    want=(radix==10 && (flags&1))?snprintf(expected,sizeof expected,format,(int)signed_value):
        snprintf(expected,sizeof expected,format,(unsigned)value);
    used=0; got=number(which,value,radix,width,flags); actual[used]=0;
    if(got!=want || used!=(unsigned)want || strcmp(actual,expected)) {
        fprintf(stderr,"kernel %u value %08x %s: [%s] %d != [%s] %d\\n",which,value,format,actual,got,expected,want);
        return 0;
    }
    ++checked; return 1;
}
int main(void) {
    const unsigned masks[]={MASKS}, bits[]={BITS}, bases[]={8,10,16};
    const uint32_t edges[]={0,1,9,10,99,100,255,256,65535,65536,0x7fffffff,0x80000000,0xffffffff};
    unsigned k,b,i,f,w; uint32_t value=0x12345678u;
    for(k=0;k<21;++k) for(b=0;b<3;++b) if(masks[k]&(1u<<b)) {
        for(i=0;i<13;++i) for(f=0;f<8;++f) if(b==1 || !(f&1))
            for(w=0;w<3;++w) if(!check(k,bits[k],edges[i],bases[b],w==0?0:w==1?11:255,f)) return 1;
        for(i=0;i<1000;++i) {
            value=value*1664525u+1013904223u;
            if(!check(k,bits[k],value,bases[b],i%3?0:11,b==1?i%8:(i%4)*2)) return 1;
        }
        if(bits[k]==16 && masks[k]==7) for(i=0;i<65536;++i)
            if(!check(k,16,i,bases[b],0,b==1?1:2)) return 1;
    }
    printf("%u\\n",checked); return 0;
}
'''.replace('MASKS', ','.join(str(r['mask']) for r in rows)).replace('BITS', ','.join(str(r['bits']) for r in rows))
(a.output/'check.c').write_text(source, encoding='utf-8', newline='\n')
subprocess.run([str(a.gcc), '-O2', '-std=c99', 'check.c', '-o', 'check.exe'], cwd=a.output, check=True)
run = subprocess.run([str((a.output/'check.exe').resolve())], capture_output=True, text=True)
(a.output/'check.log').write_text(run.stdout+run.stderr)
assert run.returncode == 0, run.stderr
result = dict(status='PASS', kernels=len(rows), cases=int(run.stdout),
              radixSubsets=7, full16BitDomain=True, native32AndSoftware32=True, hardwareExecuted=False)
(a.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
print(result)
