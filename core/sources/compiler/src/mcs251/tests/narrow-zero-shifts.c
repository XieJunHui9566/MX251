/* Sampled integers, zero extension and shifts; independent of any SDK. */
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

u32 byte_8(u32 x) ABI {return (u32)(u8)x<<8;}
u32 byte_16(u32 x) ABI {return (u32)(u8)x<<16;}
u32 byte_24(u32 x) ABI {return (u32)(u8)x<<24;}
u16 byte_word(u32 x) ABI {return (u16)((u16)(u8)x<<8);}
u32 word_8(u32 x) ABI {return (u32)(u16)x<<8;}
u32 word_16(u32 x) ABI {return (u32)(u16)x<<16;}
u32 word_24(u32 x) ABI {return (u32)(u16)x<<24;}
u32 variable_byte(u32 x,u32 y) ABI {return (u32)(u8)x<<(y&31);}
u32 variable_word(u32 x,u32 y) ABI {return (u32)(u16)x<<(y&31);}
u32 xor_byte(u32 x,u32 y) ABI {return x^(u8)y;}
u32 xor_word(u32 x,u32 y) ABI {return (u16)y^x;}
u32 or_word(u32 x,u32 y) ABI {return x|(u16)y;}
u32 and_word(u32 x,u32 y) ABI {return x&(u16)y;}
u32 signed_xor(u32 x,u32 y) ABI {return x^(u32)(s8)y;}
u32 signed_left(u32 x) ABI {return (u32)(s8)x<<16;}
u32 full_left(u32 x,u32 y) ABI {return (x<<8)^y;}
u32 reused_cast(u32 x,u32 y) ABI {
    u32 a=(u8)x;return (a<<24)^(a+y);
}
static volatile u8 __edata sample;
u32 sampled(u32 x,u32 y) ABI {
    u8 a,b;sample=(u8)x;a=sample;sample=(u8)y;b=sample;
    return ((u32)a<<24)^((u32)b<<8)^sample;
}
u32 pressure(u32 x,u32 y) ABI {
    u8 a=(u8)(x+1),b=(u8)(y+3),c=(u8)(x+5),d=(u8)(y+7);
    u16 e=(u16)(x+11),f=(u16)(y+13),g=(u16)(x+17),h=(u16)(y+19);
    u32 p=(u32)(u8)(a+19)<<24,q=(u32)(u8)(b+23)<<16;
    u32 r=(u32)(u16)(e+29)<<8,s=(u32)(u16)(f+31)<<16;
    return p^q^r^s^x^y^a^b^c^d^e^f^g^h;
}

u32 stress_run(u8 kind,u8 seed) {
    u8 j;u32 result=0,state=0x9e3779b9UL^seed;
    for(j=0;j<64;++j) {
        u32 x,y,v;state=state*1664525UL+1013904223UL;
        x=state;y=(state&0xffffff00UL)|j;
        switch(kind) {
        case 0:v=byte_8(x)^byte_16(y);break;
        case 1:v=byte_24(x)^byte_word(y);break;
        case 2:v=word_8(x)^word_16(y);break;
        case 3:v=word_24(x);break;
        case 4:v=variable_byte(x,y);break;
        case 5:v=variable_word(x,y);break;
        case 6:v=xor_byte(x,y)^xor_word(y,x);break;
        case 7:v=or_word(x,y)^and_word(y,x);break;
        case 8:v=signed_xor(x,y)^signed_left(y)^full_left(x,y);break;
        case 9:v=reused_cast(x,y);break;
        case 10:v=sampled(x,y);break;
        default:v=pressure(x,y);break;
        }
        result=(result<<1)^(result>>31)^v;
    }
    return result;
}
