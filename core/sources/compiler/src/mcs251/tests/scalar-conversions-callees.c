#include "scalar-conversions.h"
u16 sc_wake(u16 value)
{
    value = (u16)((32768UL * (u32)value) / 16000);
    if (value) --value;
    return value;
}
u32 sc_u16(u16 value, u8 seed) { return ((u32)value * 257u) ^ seed; }
u32 sc_i16(i16 value, u8 seed) { return (u32)((i32)value / 7) ^ seed; }
u32 sc_u32(u32 value, u8 seed) { return value / 16000UL + seed; }
u32 sc_i32(i32 value, u8 seed) { return (u32)(value / 1000L) ^ seed; }
u32 sc_u8(u8 value, u8 seed) { return (u32)(value * 257u) ^ seed; }
u32 sc_i8(i8 value, u8 seed) { return (u32)((i16)value * 13) ^ seed; }
u32 sc_stack(u8 seed, u16 word, i16 negative, u32 wide)
{
    return ((u32)word << 16) ^ (u32)(i32)negative ^ wide ^ seed;
}
u32 sc_change(u16 value, u8 seed)
{
    value += seed;
    return (u32)value * value;
}
u32 sc_multi(u16 value, u8 seed) { return (u32)value * value + seed; }
u32 sc_unknown(u16 value) { return (u32)value * 3; }
u32 sc_dead(u16 value) { return (u32)value * 32768UL / 16000; }
u32 sc_prune(u8 mode, u8 seed)
{
    if (mode == 2) return sc_dead(2000);
    return seed + 7u;
}
