#include "tail-boundary.h"
__sfr __at(0x89) TMOD;
__sfr __at(0xa8) IE;
__sbit __at(0x8c) TR0;
__sbit __at(0x8d) TF0;
volatile __data u8 context_image[40];
volatile __data u8 context_bank;
volatile __data u8 context_count;
volatile __data u8 context_errors;
void context_clobber(void);
void context_probe(void);

void context_irq(void) __interrupt(1)
{
    volatile u8 guard[9];
    u8 i,a,b,c;
    for (i = 0; i < 9; ++i) guard[i] = i ^ 0x5aU;
    a = context_echo(0x12U);
    b = context_echo(0x34U);
    c = context_echo(0x56U);
    context_clobber();
    if (a != (0x12U ^ 0xa5U) || b != (0x34U ^ 0xa5U) || c != (0x56U ^ 0xa5U))
        ++context_errors;
    for (i = 0; i < 9; ++i)
        if (guard[i] != (i ^ 0x5aU)) ++context_errors;
    ++context_count;
}

u32 context_check(u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL;
    IE = 0x02; TR0 = 0; TF0 = 0; TMOD = 1;
    context_bank = (seed & 3U) << 3;
    context_count = 0; context_errors = 0;
    context_probe();
    IE = 0; TF0 = 0;
    for (i = 0; i < 32; ++i) result = (result << 5) ^ result ^ context_image[i];
    result = (result << 5) ^ result ^ context_count;
    result = (result << 5) ^ result ^ context_errors;
    result ^= (u32)(context_image[32] ^ context_image[34]) << 24;
    result ^= (u32)(context_image[33] ^ context_image[35]) << 16;
    /* The stack pointer bytes are captured before and after the interrupt. */
    result ^= (u32)(context_image[36] ^ context_image[38]) << 8;
    result ^= context_image[37] ^ context_image[39];
    return result;
}
