/* Low products of sampled bytes; no SDK or peripheral dependencies. */
#ifdef __SDCC
typedef unsigned char u8;typedef signed char s8;
typedef unsigned int u16;typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef uint32_t u32;
#define ABI
#define __edata
#endif

u8 low_unsigned(u8 a,u8 b) ABI {return (u8)(a*b);}
u8 low_signed(s8 a,s8 b) ABI {return (u8)(a*b);}
u8 low_mixed(s8 a,u8 b) ABI {return (u8)(a*b);}
u8 low_constant(u8 a,u8 b) ABI {return (u8)(a*17U+b*251U);}
u32 low_live(u32 x,u32 y) ABI {
    u8 a=(u8)x,b=(u8)y,p=(u8)(a*b);
    return x^y^((u32)a<<24)^((u32)b<<16)^p;
}
u32 low_pressure(u32 x,u32 y) ABI {
    u8 a=(u8)(x+1),b=(u8)(y+3),c=(u8)(x+5),d=(u8)(y+7);
    u8 e=(u8)(x+11),f=(u8)(y+13),g=(u8)(x+17),h=(u8)(y+19);
    u8 i=(u8)(x+23),j=(u8)(y+29),k=(u8)(x+31),l=(u8)(y+37);
    u8 p=(u8)(a*b),q=(u8)(c*d),r=(u8)(e*f),s=(u8)(g*h);
    return x^y^((u32)(u8)(p*q)<<24)^((u32)(u8)(r*s)<<16)^
        ((u32)(u8)(i*j)<<8)^(u8)(k*l)^a^b^c^d^e^f^g^h^i^j^k^l;
}
static volatile s8 __edata samples[2];
u8 low_sampled(u8 a,u8 b) ABI {
    s8 x,y;samples[0]=(s8)a;samples[1]=(s8)b;x=samples[0];y=samples[1];
    return (u8)(x*y);
}
/* The high byte is observed here and must keep the full product. */
u16 full_product(u8 a,u8 b) ABI {return (u16)((u16)a*(u16)b);}

u32 stress_run(u8 kind,u8 seed) {
    u16 j;u32 result=0;
    /* Each kind covers every 8-bit factor pair over the 64 frames. */
    for(j=0;j<1024;++j) {
        u8 a=(u8)((u16)seed*4U+(j>>8)),b=(u8)j;u32 v;
        u32 x=0x12345600UL|a,y=0xa5c39e00UL|b;
        switch(kind) {
        case 0:v=low_unsigned(a,b);break;
        case 1:v=low_signed((s8)a,(s8)b);break;
        case 2:v=low_mixed((s8)a,b);break;
        case 3:v=low_constant(a,b);break;
        case 4:v=low_live(x,y);break;
        case 5:v=low_pressure(x,y);break;
        case 6:v=low_sampled(a,b);break;
        default:v=full_product(a,b);break;
        }
        result=(result<<1)^(result>>31)^v;
    }
    return result;
}
