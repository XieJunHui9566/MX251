"""实际 251 C 分支，对照 x86 SSE binary32 除法；另检查项目明确的 NaN 载荷策略。

用法：python check-fsdiv-boundaries.py HOST_GCC NEW_OUTPUT_DIRECTORY
需要支持 SSE 的 x86/x64 GCC 宿主；这不是 251 指令模拟器。
"""
from pathlib import Path
import subprocess,json,sys
S=Path(__file__).resolve().parents[3];D=Path(sys.argv[2]).resolve();D.mkdir(exist_ok=False)
s=(S/'device/lib/_fsdiv.c').read_text(encoding='utf-8').split('#elif defined(__SDCC_mcs251)\n',1)[1].split('\n#else\n',1)[0]
s=s.replace('unsigned long','uint32_t').replace('__SDCC_FLOAT_NONBANKED','').replace('__fsdiv','tested_div')
test=r'''
static uint32_t state=2512026, count=0;
static uint32_t next(void) {state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static void check(uint32_t a,uint32_t b) {
 union {float f;uint32_t u;} x,y,r,g;
 x.u=a;y.u=b;volatile float ax=x.f,bx=y.f; r.f=ax/bx;g.f=tested_div(x.f,y.f);
 uint32_t expected=r.u;
 if((a&0x7fffffffU)>0x7f800000U)expected=a|0x00400000U;
 else if((b&0x7fffffffU)>0x7f800000U)expected=b|0x00400000U;
 else if((r.u&0x7fffffffU)>0x7f800000U)expected=0x7fc00000U;
 if(expected!=g.u){fprintf(stderr,"%08x / %08x got %08x expected %08x\n",a,b,g.u,expected);exit(1);}++count;
}
int main(void) {
 uint32_t edges[]={0,0x80000000,1,2,3,0x7fffff,0x800000,0x800001,0x3f000000,0x3f800000,0x3f800001,0x40000000,0x40400000,0x7f7fffff,0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xffc12345};
 if(fesetround(FE_TONEAREST))return 2;
 _mm_setcsr(_mm_getcsr()&~0x8040U);
 for(unsigned i=0;i<19;i++)for(unsigned j=0;j<19;j++)for(unsigned sign=0;sign<4;sign++)check(edges[i]^(sign&1?0x80000000U:0),edges[j]^(sign&2?0x80000000U:0));
 for(unsigned i=0;i<2000000;i++){uint32_t a=next(),b=next();check(a,b);}
 for(unsigned i=0;i<100000;i++){uint32_t a=next()&0x807fffffU,b=next();check(a,b);check(b,a);}
 printf("%u\n",count);return 0;
}
'''
(D/'test.c').write_text('#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <fenv.h>\n#include <xmmintrin.h>\n'+s+test,encoding='utf-8')
args=[sys.argv[1],'-std=c11','-O2','-Wall','-Wextra','-frounding-math','-ffp-contract=off',str(D/'test.c'),'-o',str(D/'test.exe')]
p=subprocess.run(args,capture_output=True);(D/'build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
p=subprocess.run([str(D/'test.exe')],capture_output=True,timeout=60);(D/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
out=dict(passed=int(p.stdout),reference='x86 SSE binary32 division, FE_TONEAREST, FTZ/DAZ disabled; explicit first-NaN payload policy',targetCpuSimulated=False)
(D/'results.json').write_text(json.dumps(out,indent=2));print(out)
