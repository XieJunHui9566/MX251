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
volatile __edata s16 sampled;
__edata u32 quotients[7];
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#define ABI
volatile s16 sampled;
u32 quotients[7];
#endif

s32 product_ss(s16 a,s16 b) ABI { return (s32)a*b; }
s32 product_su(s16 a,u16 b) ABI { return (s32)a*(s32)b; }
u32 product_uu(u16 a,u16 b) ABI { return (u32)a*b; }
u32 product_modulo(s16 a,u16 b) ABI { return (u32)(s32)a*b; }
s32 product_casts(s32 a,s32 b) ABI { return (s32)(s16)a*(s16)b; }
s32 product_shared(s16 a,s16 b) ABI { s32 x=a,y=b;return x*y+x; }
s32 product_snapshot(s16 a,s16 b) ABI { s32 x;sampled=a;x=sampled;sampled=b;return x*(s32)sampled; }
u32 product_unknown(u32 a,u32 b) ABI { return a*b; }
s32 divide1(s32 a) ABI { return a/1L; }
s32 divide2(s32 a) ABI { return a/2L; }
s32 divide128(s32 a) ABI { return a/128L; }
s32 divide256(s32 a) ABI { return a/256L; }
s32 divide1024(s32 a) ABI { return a/1024L; }
s32 divide32768(s32 a) ABI { return a/32768L; }
s32 divide65536(s32 a) ABI { return a/65536L; }
s32 dividebig(s32 a) ABI { return a/1073741824L; }
s16 divide_word(s16 a) ABI { return a/256; }
s32 divide_unknown(s32 a,s32 b) ABI { return a/b; }
s32 remainder_signed(s32 a) ABI { return a%1024L; }
s32 divide_negative(s32 a) ABI { return a/-1024L; }
u32 divide_unsigned(u32 a) ABI { return a/1024UL; }
s32 ordinary_user(s32,s32) ABI;
s32 user_call(s32 a,s32 b) ABI { return ordinary_user(a,b); }

u32 quotient_mix(s32 a) ABI
{
  quotients[0]=(u32)(a/2L); quotients[1]=(u32)(a/128L);
  quotients[2]=(u32)(a/256L); quotients[3]=(u32)(a/1024L);
  quotients[4]=(u32)(a/32768L); quotients[5]=(u32)(a/65536L);
  quotients[6]=(u32)(a/1073741824L);
  return quotients[0]^quotients[1]^quotients[2]^quotients[3]^quotients[4]^quotients[5]^quotients[6];
}

u32 product_mix(s16 a,s16 b,u16 c) ABI
{
  s32 x=a,y=b;
  return (u32)(x*y) ^ (u32)(x*(s32)c) ^ (u32)(y*y) ^ ((u32)c*c);
}

/* Independent arithmetic cases also drive the board fixture. All products
 * returning signed long fit; compound mixing uses unsigned 32-bit arithmetic. */
u32 stress_run(u8 kind,u8 seed)
{
  u8 i;u32 result=0x12345678UL;
  for(i=0;i<4;++i) {
    u16 x=(u16)seed*4U+i;
    s16 a=(s16)(u16)((u32)x*257UL),b=(s16)(u16)(65535UL-(u32)x*251UL);
    u16 ub=(u16)((u32)x*251UL+32769UL);
    s32 v=(s32)((u32)x*0x01010101UL), q;
    if(x==0) {a=-32767-1;b=-32767-1;v=(-2147483647L-1L);}
    else if(x==1) {a=32767;b=-1;v=2147483647L;}
    else if(x==2) {a=-1;b=32767;v=-1025L;}
    else if(x==3) {a=0;b=-1;v=-1024L;}
    else if(x==4)v=-1023L;
    else if(x==5)v=-1L;
    else if(x==6)v=0;
    else if(x==7)v=1;
    if(kind==0)q=product_ss(a,b);
    else if(kind==1)q=product_su(a,ub);
    else if(kind==2)q=(s32)(product_uu((u16)a,ub)^product_modulo(a,ub));
    else if(kind==3)q=product_casts(v,(s32)b)^product_shared(a,b);
    else if(kind==4)q=product_snapshot(a,b);
    else if(kind==5)q=divide2(v)^divide128(v)^divide256(v);
    else if(kind==6)q=divide1024(v)^divide32768(v);
    else if(kind==7)q=divide65536(v)^dividebig(v);
    else if(kind==8)q=(s32)divide_word(a)^(s32)divide_unsigned((u32)v);
    else if(kind==9)q=(s32)quotient_mix(v);
    else q=(s32)product_mix(a,b,ub);
    result=(result<<3)^(result>>5)^(u32)q;
  }
  return result;
}
