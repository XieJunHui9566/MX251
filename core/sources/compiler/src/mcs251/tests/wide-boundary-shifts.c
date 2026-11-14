/* Constant right shifts around byte boundaries, independent of any SDK. */
#ifdef __SDCC
typedef unsigned char u8;typedef unsigned int u16;
typedef unsigned long u32;typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
#define ABI
#define __edata
#endif

#define SHIFT(k) \
u32 logical_##k(u32 x,u32 y) ABI {return x>>k;} \
u32 signed_##k(u32 x,u32 y) ABI {return (u32)((s32)x>>k);}
SHIFT(7) SHIFT(15) SHIFT(23) SHIFT(31)

#define VALUES \
u32 a=(x+0x01234567UL)^y,b=(x+0x12345678UL)^y,c=(x+0x23456789UL)^y; \
u32 d=(x+0x3456789aUL)^y,e=(x+0x456789abUL)^y,f=(x+0x56789abcUL)^y; \
u32 g=(x+0x6789abcdUL)^y,h=(x+0x789abcdeUL)^y,i=(x+0x89abcdefUL)^y
u32 pressure_logical(u32 x,u32 y) ABI {
    VALUES;
    return ((a>>7)+(b>>15)+(c>>23)+(d>>31)+(e>>7)+(f>>15)+(g>>23)+(h>>31)+(i>>15))^
        a^b^c^d^e^f^g^h^i;
}
u32 pressure_signed(u32 x,u32 y) ABI {
    VALUES;
    return ((u32)((s32)a>>7)+(u32)((s32)b>>15)+(u32)((s32)c>>23)+
        (u32)((s32)d>>31)+(u32)((s32)e>>7)+(u32)((s32)f>>15)+
        (u32)((s32)g>>23)+(u32)((s32)h>>31)+(u32)((s32)i>>15))^
        a^b^c^d^e^f^g^h^i;
}
static volatile u32 __edata sample;
u32 sampled_logical(u32 x,u32 y) ABI {
    sample=x;return (sample>>7)^(sample>>15)^(sample>>23)^sample;
}
u32 sampled_signed(u32 x,u32 y) ABI {
    sample=x;return (u32)((s32)sample>>7)^(u32)((s32)sample>>15)^
        (u32)((s32)sample>>23)^(u32)((s32)sample>>31)^sample;
}
/* Nearby counts and variable shifts must retain their existing lowering. */
u32 unchanged_other(u32 x,u32 y) ABI {return (x>>6)^(x>>8)^(x>>14)^(x>>16)^(x>>22);}
u32 unchanged_variable(u32 x,u32 y) ABI {return (x>>(y&31))^(u32)((s32)x>>(y&31));}

u32 stress_run(u8 kind,u8 seed) {
    u32 result=0,state=0x9e3779b9UL^((u32)seed*0x01010101UL);u8 j;
    for(j=0;j<40;++j) {
        u32 x,y,v;
        state=state*1664525UL+1013904223UL;
        /* Walking bits/complements, sign extremes, and independent halves. */
        if(j<32)x=(1UL<<j)^((seed&1)?0xffffffffUL:0);
        else if(j==32)x=0;
        else if(j==33)x=0xffffffffUL;
        else if(j==34)x=0x7fffffffUL;
        else if(j==35)x=0x80000000UL;
        else x=state;
        y=state^((u32)seed<<24);
        switch(kind) {
        case 0:v=logical_7(x,y);break;case 1:v=logical_15(x,y);break;
        case 2:v=logical_23(x,y);break;case 3:v=logical_31(x,y);break;
        case 4:v=signed_7(x,y);break;case 5:v=signed_15(x,y);break;
        case 6:v=signed_23(x,y);break;case 7:v=signed_31(x,y);break;
        case 8:v=pressure_logical(x,y);break;case 9:v=pressure_signed(x,y);break;
        case 10:v=sampled_logical(x,y);break;default:v=sampled_signed(x,y);break;
        }
        result=(result<<1)^(result>>31)^v;
    }
    return result;
}
