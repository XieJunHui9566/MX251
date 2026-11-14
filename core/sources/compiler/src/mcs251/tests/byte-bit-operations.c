/* Exhaust every byte in the board fixture, without a device or SDK header. */
#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define __edata
#define ABI
#endif

static volatile u8 __edata cell;

#define BYTE_CASE(N) \
u32 byte_##N(u8 x) ABI { \
    u8 i,v,l,r; signed char s; u32 result=0; \
    for(i=0;i<3;++i) { \
        v=(u8)(x+(u16)i*83U); \
        l=(u8)(v<<N); r=(u8)(v>>N); s=(signed char)((signed char)v>>N); \
        result=(result<<3)^((u32)l<<16)^((u16)r<<8)^(u8)s; \
        cell=v; \
        if((cell>>N)&1)result^=0x12345678UL; \
        if((cell>>N)&3)result+=0x1357U; \
        if((signed char)cell>>N)result+=0x2468U; \
        result^=cell; \
    } \
    return result; \
}
BYTE_CASE(0)
BYTE_CASE(1)
BYTE_CASE(2)
BYTE_CASE(3)
BYTE_CASE(4)
BYTE_CASE(5)
BYTE_CASE(6)
BYTE_CASE(7)

u32 stress_run(u8 kind,u8 seed) {
    u8 i,x;u32 result=0x12345678UL,a;
    for(i=0;i<4;++i) {
        x=(u8)((u16)seed*4U+i);
        switch(kind) {
        case 0:a=byte_0(x);break;
        case 1:a=byte_1(x);break;
        case 2:a=byte_2(x);break;
        case 3:a=byte_3(x);break;
        case 4:a=byte_4(x);break;
        case 5:a=byte_5(x);break;
        case 6:a=byte_6(x);break;
        default:a=byte_7(x);break;
        }
        result=(result<<3)^(result>>5)^a;
    }
    return result;
}
