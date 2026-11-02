/* Exercise the exact optimizer state machine independently of SDCC. GPL-2.0-or-later. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../transfer_values.h"
static uint32_t seed = 0x25123001;
static uint32_t random32(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned checked, removed;
static unsigned dead_checked, dead_removed;
static uint32_t word(const unsigned char *p,unsigned w) { uint32_t v=0;for(unsigned i=0;i<w;++i)v=(v<<8)|p[i];return v; }
static void putword(unsigned char *p,unsigned w,uint32_t v) { while(w){p[--w]=v;v>>=8;} }
static void test_dead(void)
{
  unsigned live=MCS251_TRANSFER_ALL_LIVE;
  CHECK(!mcs251TransferDead(&live,"mov dr4,dr16"));
  CHECK(!mcs251TransferDead(&live,"add dr16,dr20"));
  CHECK(mcs251TransferDead(&live,"mov dr4,dr16"));
  live=MCS251_TRANSFER_ALL_LIVE;
  CHECK(!mcs251TransferDead(&live,"mov wr4,wr20"));
  CHECK(!mcs251TransferDead(&live,"mov dr4,dr16"));
  const unsigned words[]={0,2,4,6,8,12,14,16,18,20,22},dwords[]={0,4,12,16,20},bytes[]={8,9,12,13,14,15};
  const char *names[]={"mov","add","sub","anl","orl","xrl","cmp","ecall"};
  for(unsigned block=0;block<16384;++block)
    {
      struct { unsigned op,d,s,w,salt; int drop;char text[64]; } ops[64];
      unsigned char a[24],b[24];
      for(unsigned i=0;i<24;++i)a[i]=b[i]=random32();
      for(unsigned i=0;i<64;++i)
        {
          unsigned op=random32()%8,w=(op==0 && !(random32()&3))?1:2u<<(random32()&1);
          const unsigned *regs=w==1?bytes:w==2?words:dwords;unsigned n=w==1?6:w==2?11:5;
          ops[i].op=op;ops[i].d=regs[random32()%n];ops[i].s=regs[random32()%n];ops[i].w=w;ops[i].salt=random32();
          snprintf(ops[i].text,sizeof ops[i].text,"%s %s%u,%s%u",names[op],w==1?"r":w==2?"wr":"dr",ops[i].d,w==1?"r":w==2?"wr":"dr",ops[i].s);
        }
      live=MCS251_TRANSFER_ALL_LIVE;
      for(int i=63;i>=0;--i){ops[i].drop=mcs251TransferDead(&live,ops[i].text);dead_removed+=ops[i].drop;++dead_checked;}
      for(unsigned i=0;i<64;++i)
        {
          unsigned op=ops[i].op,d=ops[i].d,s=ops[i].s,w=ops[i].w;
          if(op==7)
            {
              CHECK(!memcmp(a,b,24));
              for(unsigned j=0;j<24;++j)a[j]=b[j]=(ops[i].salt>>(j%24))^j;
            }
          else if(op==0)
            { unsigned char tmp[4];memcpy(tmp,a+s,w);memcpy(a+d,tmp,w);if(!ops[i].drop){memcpy(tmp,b+s,w);memcpy(b+d,tmp,w);} }
          else
            {
              uint32_t x=word(a+d,w),y=word(a+s,w);CHECK(x==word(b+d,w)&&y==word(b+s,w));
              if(op!=6){uint32_t z=op==1?x+y:op==2?x-y:op==3?x&y:op==4?x|y:x^y;putword(a+d,w,z);putword(b+d,w,z);}
            }
        }
      CHECK(!memcmp(a,b,24));
    }
  CHECK(dead_removed>0);
}
static void step(mcs251_transfer_values *s, uint32_t a[24], uint32_t b[24], unsigned d,unsigned src,unsigned width)
{
  char line[80]; const char *kind=width==4?"dr":width==2?"wr":"r"; uint32_t snap[4];
  snprintf(line,sizeof line,"mov %s%u,%s%u",kind,d,kind,src);
  int drop=mcs251TransferApply(s,line); removed+=drop; ++checked;
  for(unsigned i=0;i<width;++i)snap[i]=a[src+i];
  for(unsigned i=0;i<width;++i)a[d+i]=snap[i];
  if(!drop) { for(unsigned i=0;i<width;++i)snap[i]=b[src+i]; for(unsigned i=0;i<width;++i)b[d+i]=snap[i]; }
  if(memcmp(a,b,24*sizeof *a))fprintf(stderr,"step %u seed %08x: %s drop=%d\n",checked,seed,line,drop);
  CHECK(!memcmp(a,b,24*sizeof *a));
}
int main(void)
{
  mcs251_transfer_values state; mcs251_transfer_register d,s; uint32_t a[24],b[24];
  const char *invalid[]={"", "mov", "mov wr16", "mov wr16,wr4 extra", "mov wr17,wr4", "mov dr2,dr16",
    "mov wr10,wr16", "mov dr8,dr16", "mov dr24,dr16", "mov r16,r12", "mov r4,r8", "mov a,r8", "mov ar4,r8",
    "mov wr16,@spx", "mov @spx,wr16", "mov wr16,#0", "mov wr16,dr4", "mov wr16,wr9999999999", "movx a,@dptr"};
  for(unsigned i=0;i<sizeof invalid/sizeof *invalid;++i)CHECK(!mcs251TransferParse(invalid[i],&d,&s));
  mcs251TransferReset(&state);
  CHECK(!mcs251TransferApply(&state,"mov dr16,dr4"));
  CHECK(!mcs251TransferApply(&state,"mov wr12,wr18"));
  CHECK(!mcs251TransferApply(&state,"mov wr14,wr16"));
  CHECK(mcs251TransferApply(&state,"mov wr18,wr12"));
  CHECK(mcs251TransferApply(&state,"mov wr16,wr14"));
  CHECK(!mcs251TransferApply(&state,"mov r12,r15"));
  CHECK(!mcs251TransferApply(&state,"mov wr18,wr12"));
  CHECK(!mcs251TransferApply(&state,"ecall _callee"));
  CHECK(!mcs251TransferApply(&state,"mov wr16,wr14"));
  mcs251TransferReset(&state);
  CHECK(!mcs251TransferApply(&state,"mov dr20,dr4"));
  CHECK(!mcs251TransferApply(&state,"add dr16,dr20"));
  CHECK(mcs251TransferApply(&state,"mov dr20,dr4"));
  CHECK(!mcs251TransferApply(&state,"mov dr16,dr4"));
  CHECK(!mcs251TransferApply(&state,"sub wr18,wr16"));
  CHECK(!mcs251TransferApply(&state,"mov dr16,dr4"));
  const char *barriers[]={"ecall f", "L1:", "mov @spx,wr16", "mov wr16,@spx", "mov psw,#0", "mul wr16,wr20", "push wr16", "pop wr16", ".db 0"};
  for(unsigned block=0;block<32768;++block)
    {
      mcs251TransferReset(&state);
      for(unsigned i=0;i<24;++i)a[i]=b[i]=random32();
      for(unsigned j=0;j<64;++j)
        {
          if(!(random32()&15))
            {
              CHECK(!mcs251TransferApply(&state,barriers[random32()%9]));
              /* 未知指令允许任意写入，证明不会带着旧等价关系越界。 */
              for(unsigned i=0;i<24;++i)a[i]=b[i]=random32();
            }
          unsigned width=1u<<(random32()%3),dst=(random32()%(24/width))*width,src=(random32()%(24/width))*width;
          step(&state,a,b,dst,src,width);
          if(!(random32()&3))step(&state,a,b,src,dst,width);
          if(!(random32()&3))
            {
              unsigned w=2u<<(random32()&1),d=(random32()%(24/w))*w,src=(random32()%(24/w))*w;
              const char *ops[]={"add","sub","anl","orl","xrl","cmp"};unsigned op=random32()%6;char line[80];
              snprintf(line,sizeof line,"%s %s%u,%s%u",ops[op],w==2?"wr":"dr",d,w==2?"wr":"dr",src);
              CHECK(!mcs251TransferApply(&state,line));
              /* 参考模型只声明允许写的字节，不复制优化器的等价关系实现。
               * 对目标施加任意新值，比某一个算术输入的实际结果更一般。 */
              if(op!=5)for(unsigned k=0;k<w;++k)a[d+k]=b[d+k]=random32();
              CHECK(!memcmp(a,b,sizeof a));
            }
        }
    }
  CHECK(removed>10000);
  test_dead();
  printf("{\"transfers\":%u,\"eliminated\":%u,\"randomBlocks\":32768,\"deadWriteInstructions\":%u,\"deadWritesRemoved\":%u,\"invalidForms\":%u,\"passed\":true}\n",checked,removed,dead_checked,dead_removed,(unsigned)(sizeof invalid/sizeof *invalid));
}
