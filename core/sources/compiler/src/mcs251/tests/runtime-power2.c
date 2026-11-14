#if defined(__SDCC) || defined(__C251__)
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#endif
#ifdef __SDCC
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#define ABI
#endif

/* Promotions hide the unsigned origin; all four dividends stay >= 0. */
u16 promote_mod(u8 x) ABI { return (x + 1) % 16; }
u16 promote_div(u8 x) ABI { return (x + 1) / 16; }
u32 wide_mod(u16 x) ABI { return ((s32)x + 1L) % 256L; }
u32 wide_div(u16 x) ABI { return ((s32)x + 1L) / 256L; }

/* A signed cast or a negative input must retain C's remainder semantics. */
s16 negative_mod(s16 x) ABI { return x % 16; }
s16 negative_div(s16 x) ABI { return x / 16; }
s16 wrapped_mod(u16 x) ABI { return (s16)(u16)(x + 1U) % 16; }
s16 negative_divisor(s16 x) ABI { return x % -16; }

#ifdef __SDCC
volatile __edata u8 sampled;
#elif defined(__C251__)
volatile u8 edata sampled;
#else
volatile u8 sampled;
#endif
u16 volatile_mod(u8 x) ABI
{
    u16 a, b;
    sampled = x;
    a = (sampled + 1) % 16;
    b = (sampled + 2) / 8;
    return (a << 8) ^ b;
}
