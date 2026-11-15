#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 4
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#define RAM __edata
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#define ABI
#define RAM
#endif

volatile u16 observation;
volatile s16 moving;
RAM u8 data[64];

u16 observe(u16 x, u8 seed) ABI { observation=(u16)(x+seed); return observation; }
void impossible(void) ABI { observation=0xdead; }
void change_index(s16 *p, u8 *once) ABI
{
  if (*p==2 && !*once) { *p=-3; *once=1; }
}

u16 loop_small(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=0;i<30;++i) {
    sum=(u16)(sum+observe((u16)(i%32),seed));
    if(i%32==31)impossible();
  }
  return (u16)(sum+i);
}

u16 loop_large(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=0;i<500;++i)sum=(u16)(sum+observe((u16)(i%32),seed));
  return (u16)(sum+i);
}

u16 loop_negative(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=-37;i<37;i+=3)sum=(u16)(sum+(u16)(i%8));
  return (u16)(sum+(u16)i);
}

u16 loop_down(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=37;i>=-37;i-=3)sum=(u16)(sum+(u16)(i%8));
  return (u16)(sum+(u16)i);
}

u16 loop_exits(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=0;i<61;i+=2) {
    if(i>seed)break;
    if(i%4==2)continue;
    sum=(u16)(sum+observe((u16)(i%32),seed));
  }
  return (u16)(sum+i);
}

u16 loop_empty(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=7;i<3;++i)sum=(u16)(sum+observe((u16)i,seed));
  return (u16)(sum+i);
}

u16 loop_once(u8 seed) ABI
{
  s16 i=37; u16 sum=seed;
  do {sum=(u16)(sum+(u16)(i%32)); i+=3;} while(i<30);
  return (u16)(sum+i);
}

u16 loop_redefine(u8 seed) ABI
{
  s16 i; u16 sum=seed; u8 once=0;
  for(i=0;i<30;++i) {
    if(i==2 && !once) {i=-3;once=1;}
    sum=(u16)(sum+(u16)(i%32));
  }
  return (u16)(sum+i);
}

u16 loop_escape(u8 seed) ABI
{
  s16 i; u16 sum=seed; u8 once=0;
  for(i=0;i<30;++i) {
    change_index(&i,&once);
    sum=(u16)(sum+(u16)(i%32));
  }
  return (u16)(sum+i);
}

u16 loop_volatile(u8 seed) ABI
{
  u16 sum=seed;
  for(moving=-3;moving<30;++moving)sum=(u16)(sum+(u16)(moving%32));
  return (u16)(sum+(u16)moving);
}

u16 loop_wrap(u8 seed) ABI
{
  u8 i=250; u16 sum=seed;
  do {sum=(u16)(sum+i%32);i=(u8)(i+3);} while(i<254);
  return (u16)(sum+i);
}

u16 loop_nested(u8 seed) ABI
{
  s16 i,j; u16 sum=seed;
  for(i=0;i<3;++i)for(j=-7;j<17;++j)sum=(u16)(sum+(u16)((i+j)%8));
  return (u16)(sum+i+j);
}

u16 loop_dynamic(u8 seed) ABI
{
  s16 i=(s16)seed-32; u16 sum=seed;
  while(i<30) {sum=(u16)(sum+(u16)(i%32));++i;}
  return (u16)(sum+i);
}

u16 loop_address(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=0;i<64;++i)data[i]=(u8)(seed+i);
  for(i=0;i<64;++i)sum=(u16)(sum+data[i]+i%16);
  return (u16)(sum+i);
}

u16 loop_entries(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  if(seed&1) {i=-3;goto body;}
  i=0;
  do {
body:
    sum=(u16)(sum+(u16)(i%32));
    ++i;
  } while(i<30);
  return (u16)(sum+i);
}

u16 loop_multiupdate(u8 seed) ABI
{
  s16 i; u16 sum=seed;
  for(i=0;i<30;++i) {
    sum=(u16)(sum+(u16)(i%32));
    if(i==2)i=-3;
    if(i<0) {sum=(u16)(sum+(u16)(i%32));i=7;continue;}
  }
  return (u16)(sum+i);
}

u16 loop_wordwrap(u8 seed) ABI
{
  u16 i=65530U,sum=seed; u8 count=0;
  do {
    sum=(u16)(sum+i%32);i=(u16)(i+7U);
    if(++count>seed+2)break;
  } while(i<65532U);
  return (u16)(sum+i);
}

u16 loop_boundary(u8 seed) ABI
{
  s16 i;u16 sum=seed;
  for(i=32760;i<=32766;++i)sum=(u16)(sum+(u16)(i%32));
  return (u16)(sum+i);
}

u16 loop_mixed(u8 seed) ABI
{
  s16 i=-3;u16 sum=seed;u8 count=0;
  do {
    sum=(u16)(sum+(u16)(i%32));--i;
    if(++count>seed+2)break;
  } while((u16)i>30U);
  return (u16)(sum+(u16)i);
}

u16 loop_long(u8 seed) ABI
{
  s32 i;u16 sum=seed;
  for(i=59977L;i<60000L;i+=3L)sum=(u16)(sum+(u16)(i%32L));
  return (u16)(sum+(u16)i);
}

u32 stress_run(u8 kind,u8 seed)
{
  if(kind==0)return loop_small(seed);
  if(kind==1)return loop_large(seed);
  if(kind==2)return loop_negative(seed);
  if(kind==3)return loop_down(seed);
  if(kind==4)return loop_exits(seed);
  if(kind==5)return loop_empty(seed);
  if(kind==6)return loop_once(seed);
  if(kind==7)return loop_redefine(seed);
  if(kind==8)return loop_escape(seed);
  if(kind==9)return loop_volatile(seed);
  if(kind==10)return loop_wrap(seed);
  if(kind==11)return loop_nested(seed);
  if(kind==12)return loop_dynamic(seed);
  if(kind==13)return loop_address(seed);
  if(kind==14)return loop_entries(seed);
  if(kind==15)return loop_multiupdate(seed);
  if(kind==16)return loop_wordwrap(seed);
  if(kind==17)return loop_boundary(seed);
  if(kind==18)return loop_mixed(seed);
  return loop_long(seed);
}
