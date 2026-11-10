#include "shared-tails.h"
u32 tail_case(u8 kind, u8 seed)
{
    u8 i;
    u32 result = 0x13579bdfUL ^ seed;
    for (i = 0; i < 32; ++i) TREG(i) = (u8)(seed * 13 + i * 7);
    tail_trim = seed ^ 0x55; tail_vtrim = seed ^ 0xaa;
    tail_band = seed * 3; tail_frequency = 1234567UL + seed;
    if (kind) tail_clock(seed & 7);
    else tail_pwm(seed & 3, (u16)(seed * 37) ^ 0x5a3);
    result ^= tail_result(seed & 1, (u16)(seed * 997));
    result ^= (u32)tail_dependencies(seed & 2, (u16)(seed * 1073)) << 16;
    for (i = 0; i < 32; ++i) result = (result << 5) ^ result ^ TREG(i);
    result = (result << 5) ^ result ^ tail_trim;
    result = (result << 5) ^ result ^ tail_vtrim;
    result = (result << 5) ^ result ^ tail_band;
    return result ^ tail_frequency;
}
