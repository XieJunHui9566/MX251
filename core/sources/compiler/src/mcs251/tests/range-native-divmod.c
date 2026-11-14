#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#define __edata
#define ABI
#endif

u16 promoted_mod(u8 x,u8 y) ABI {return (x+y)%31;}
u16 promoted_div(u8 x,u8 y) ABI {return (x+y)/10;}
u16 variable_div(u8 x,u8 y) ABI {return (x+257)/((y&31)+1);}
u16 variable_mod(u8 x,u8 y) ABI {return (x+257)%(y+1);}
u16 bounded_div(s16 x) ABI {return (x&32767)/7;}
u16 unsigned_mod(u16 x) ABI {return x%257U;}

u32 paired(u8 x,u8 y) ABI {
    s16 n=x+257,d=y+1;
    u16 q=n/d,r=n%d;
    return ((u32)q<<16)^r;
}

/* These domains include negative values, or exceed the native word width. */
s16 negative_div(s16 x) ABI {return x/31;}
s16 negative_mod(s16 x) ABI {return x%31;}
s16 negative_divisor(u8 x) ABI {return (x+257)%-7;}
s16 wrapped_mod(u16 x) ABI {return (s16)(u16)(x+1U)%31;}
s16 unknown_divisor(u8 x,s16 y) ABI {return (x+257)/y;}
u32 wide_div(u16 x) ABI {return ((s32)x+65536L)/31L;}

static volatile u8 __edata sampled;
u16 volatile_mod(u8 x) ABI {
    u16 a,b;
    sampled=x;
    a=(sampled+1)%31;
    b=(sampled+2)/7;
    return (a<<8)^b;
}

u32 stress_run(u8 kind,u8 seed) {
    u8 i,x,y;u16 w; s16 s,d;u32 result=0x12345678UL,a;
    for(i=0;i<4;++i) {
        x=(u8)((u16)seed*4U+i);y=(u8)(255-x);w=(u16)x*257U;
        if(x==127)w=32767U;
        if(x==128)w=32768U;
        if(x==254)w=65534U;
        if(x==255)w=65535U;
        s=(s16)w;d=(s16)x-128;if(!d)d=1;
        switch(kind) {
        case 0:a=((u32)promoted_mod(x,x)<<24)^((u32)promoted_mod(x,(u8)(x+1))<<16)^promoted_div(x,x);break;
        case 1:a=((u32)variable_div(x,y)<<16)^variable_mod(x,x);break;
        case 2:a=paired(x,y);break;
        case 3:a=((u32)bounded_div(s)<<16)^unsigned_mod(w);break;
        case 4:a=((u32)(u16)negative_div(s)<<16)^(u16)negative_mod(s);break;
        case 5:a=((u32)(u16)negative_divisor(x)<<16)^(u16)wrapped_mod(w);break;
        case 6:a=wide_div(w)^((u32)(u16)unknown_divisor(x,d)<<16);break;
        default:a=volatile_mod(x);break;
        }
        result=(result<<3)^(result>>5)^a;
    }
    return result;
}
