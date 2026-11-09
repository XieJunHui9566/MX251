#include "leaf.h"
volatile u8 sampled_a, sampled_b;
u8 leaf_mix(u8 a, u8 b)
{
    sampled_a = a;
    sampled_b = b;
    return (a ^ (u8)(b + 0x5b)) + (a >> 3);
}
