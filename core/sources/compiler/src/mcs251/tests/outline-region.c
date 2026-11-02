/* Production grammar boundaries: compile on host, not MCU. */
#include <assert.h>
#include <string.h>
#include "../outline_region.h"
int main(void) {
 char key[512], body[512];
 const char *good[]={"mov wr14,@spx-0x0001","mov @spx-0x0000,a","mov @spx-32765,r0","mov @dpx,wr12","mov dpx,dr16","add wr18,#_table","addc a,#(_table>>16)","movc a,@a+dptr","mul ab","inc dpx,#2","rlc a"};
 const char *bad[]={"mov spx,dr0","mov dr0,spx","mov a,_SP","mov a,0x81","mov _IE,a","mov a,_TH0","mov c,_TF0","mov @spx+1,r0","mov @spx,r0","mov @spx-32766,r0","mov @spx--1,r0","mov @spx-99999999999999,r0","mov r0,#$+2","mov r0,#00100$","movc a,@a+pc","ecall _f","push r0","pop r0","sjmp 00100$","eret","setb _TR0","db 1","mov dr0,dr60"};
 for (unsigned i=0;i<sizeof(good)/sizeof(*good);++i) assert(m251OutlineParse(good[i],key,body));
 for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) assert(!m251OutlineParse(bad[i],key,body));
 /* direct-byte 别名不能扩散到间接、位和宽视图；所有原生字节 Rn 可读取。 */
 for (unsigned r=0;r<16;++r) {
   char src[64];snprintf(src,sizeof src,"mov r%u,_port",r);
   assert(m251OutlineParseRead(src,key,body,"_port",0));assert(!strcmp(src,body));
   assert(!m251OutlineParseRead(src,key,body,"_port",1));
 }
 for (unsigned r=0;r<8;++r) {
   char src[64];snprintf(src,sizeof src,"orl ar%u,a",r);
   assert(m251OutlineParse(src,key,body));assert(!strcmp(src,body));
   snprintf(src,sizeof src,"mov a,ar%u",r);assert(m251OutlineParse(src,key,body));
   snprintf(src,sizeof src,"mov a,@ar%u",r);assert(!m251OutlineParse(src,key,body));
   snprintf(src,sizeof src,"clr ar%u",r);assert(!m251OutlineParse(src,key,body));
 }
 const char *aliasBad[]={"mov wr0,ar0","mov ar0,dr4","mov ar8,a","mov ar00,a","mov ar2,@spx-1","movz ar2,a","orl ar2,r9","mov @ar0,a","push ar0","pop ar0","mov ar0,_port"};
 for (unsigned i=0;i<sizeof(aliasBad)/sizeof(*aliasBad);++i) assert(!m251OutlineParse(aliasBad[i],key,body));
 const char *readBad[]={"mov wr0,_port","mov dr4,_port","mov r16,_port","mov c,_port","mov _port,r9","mov ar0,_port","mov spx,_port"};
 for (unsigned i=0;i<sizeof(readBad)/sizeof(*readBad);++i) assert(!m251OutlineParseRead(readBad[i],key,body,"_port",0));
 assert(m251OutlineParse(good[0],key,body));assert(!strcmp(body,"mov wr14,@spx-0x0004"));
 /* 对全部合法负位移验证原物理地址，而不是只检查格式。 */
 for (unsigned off=0;off<=32765;++off) {
   char src[64];snprintf(src,sizeof src,"mov r0,@spx-0x%04x",off);
   assert(m251OutlineParse(src,key,body));
   long repaired=strtol(strstr(body,"0x"),0,0);
   assert(0x9000+3-repaired==0x9000-off);
 }
 puts("grammar and 32766 stack-address equivalences passed");return 0;
}
