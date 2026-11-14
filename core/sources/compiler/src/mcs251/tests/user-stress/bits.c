#include "stress.h"
typedef struct {
    unsigned char count : 4;
    unsigned char mode : 3;
    unsigned char ready : 1;
    signed char bias : 5;
    unsigned char keep : 3;
} Flags;
static volatile __edata Flags flags;

u32 bits_clean(u8 seed)
{
    u8 i; u32 result = 0;
    flags.keep = seed & 7;
    for (i = 0; i < 24; ++i) {
        flags.count = (seed + i) & 15;
        flags.mode = i & 7; flags.ready = i & 1;
        flags.bias = (signed char)((seed + i) % 31) - 15;
        result = (result << 2) ^ flags.count ^ ((u16)flags.mode << 8);
        result += (u16)flags.bias;
        if (flags.ready) result ^= flags.keep;
    }
    return result;
}

u32 bits_messy(u8 seed)
{
    volatile Flags *p = &flags, *alias = p; u8 i = 0; u32 result = 0;
    alias->keep = seed & 7;
again:
    p->count = (seed + i) & 15;
    alias->mode = i & 7; p->ready = i & 1;
    alias->bias = (signed char)((seed + i) % 31) - 15;
    result = (result << 2) ^ alias->count ^ ((u16)p->mode << 8);
    result += (u16)p->bias;
    if (!alias->ready) goto tail;
    result ^= p->keep;
tail:
    if (++i != 24) goto again;
    return result;
}
