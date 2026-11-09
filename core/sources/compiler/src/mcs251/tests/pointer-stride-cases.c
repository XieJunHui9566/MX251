#include "pointer-stride.h"

static u8 __xdata octets[4096];
static u16 __xdata words[1024];

void stride_init(void)
{
    u16 i;
    for (i = 0; i < 4096; ++i)
        octets[i] = (u8)((i * 17) ^ (i >> 8) ^ 0x5a);
    for (i = 0; i < 1024; ++i)
        words[i] = 0x3456 ^ (i * 29);
}

u32 stride_case(u8 seed)
{
    u32 result = 0x13579bdfUL;
    u8 saved, value;
#define MIX(v) result = (result << 5) ^ result ^ (v)
    MIX(stride_signed(octets, 64 + seed));
    MIX(stride_signed(octets, 128 + seed));
    MIX(stride_signed(octets, 1536 + seed));
    MIX(stride_signed(octets + 2048, -129 - (int)seed));
    MIX(stride_unsigned(octets, 0x8100U + seed));
    MIX(stride_nested(octets, 256 + seed));
    MIX(stride_subscript(octets, 256 + seed));
    MIX(stride_volatile(octets, 256 + seed));
    MIX(stride_word(words, 256 + seed));
    MIX(stride_wrapped(octets, 128 + seed));
    saved = octets[2 * (320 + (u16)seed)];
    value = seed ^ 0xa5;
    stride_store(octets, 320 + seed, value);
    MIX(octets[2 * (320 + (u16)seed)]);
    stride_store(octets, 320 + seed, saved);
    return result;
}
