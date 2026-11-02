"""实际 C 分支的 32 位宿主语义测试，以独立 64 位乘积作为参考。"""
from pathlib import Path
import json,subprocess,sys
if len(sys.argv)!=3:raise SystemExit('usage: check-mullong.py HOST_CC NEW_OUTPUT_DIRECTORY')
D=Path(sys.argv[2]).resolve();D.mkdir(parents=True,exist_ok=False)
s=(Path(__file__).resolve().parents[3]/'device/lib/_mullong.c').read_text(encoding='utf-8')
s=s.split('#elif defined(__SDCC_mcs251)',1)[1].split('\n',1)[1].split('#elif defined(__SDCC_z80)',1)[0]
s=s.replace('unsigned long','uint32_t').replace('unsigned int','uint16_t').replace('long','int32_t').replace('__SDCC_NONBANKED','')
# 替换类型后函数名中的 long 也会受影响，统一为测试入口。
s=s.replace('_mulint32_t','tested_multiply')
test=r'''
static uint32_t state=25132;
static uint32_t next_value(void) { state=state*1664525U+1013904223U;return state; }
static void check(uint32_t a,uint32_t b) {
 uint32_t expected=(uint32_t)((uint64_t)a*(uint64_t)b);
 uint32_t got=(uint32_t)tested_multiply((int32_t)a,(int32_t)b);
 if(expected!=got) {fprintf(stderr,"failed %08x %08x\n",a,b);exit(1);}
}
int main(void) {
 uint32_t edge[]={0,1,2,0xff,0x100,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000U,0xffffffffU};
 for(unsigned i=0;i<12;i++)for(unsigned j=0;j<12;j++)check(edge[i],edge[j]);
 for(unsigned i=0;i<1000000;i++){uint32_t a=next_value();uint32_t b=next_value();check(a,b);}
 puts("1000144");return 0;
}
'''
(D/'test.c').write_text('#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n'+s+test,encoding='utf-8')
args=[sys.argv[1],'-std=c11','-O2',str(D/'test.c'),'-o',str(D/'test.exe')]
p=subprocess.run(args,capture_output=True);(D/'build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
p=subprocess.run([str(D/'test.exe')],capture_output=True,timeout=30);(D/'run.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr
result=dict(passed=int(p.stdout),reference='Independent uint64_t multiply truncated to uint32_t',targetCpuSimulated=False)
(D/'results.json').write_text(json.dumps(result,indent=2));print(result)
