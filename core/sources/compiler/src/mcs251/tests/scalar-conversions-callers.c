#include "scalar-conversions.h"
volatile u16 sc_input;
static u32 mix(u32 hash, u32 value) { return (hash << 5) ^ hash ^ value; }
u32 scalar_case(u8 kind, u8 seed)
{
    u32 hash = 0x13579bdfUL;
    if (!kind) {
        hash = mix(hash, sc_u16(2000, seed));
        hash = mix(hash, sc_i16(-2000, seed));
        hash = mix(hash, sc_u32(-1, seed));
        hash = mix(hash, sc_i32(0x80000001UL, seed));
        hash = mix(hash, sc_u8((u8)0x1234u, seed));
        hash = mix(hash, sc_i8((i8)0xffu, seed));
        return mix(hash, sc_stack(seed, (u16)0x12345UL, -7, 0xffffffffUL));
    }
    hash = mix(hash, sc_wake(2000));
    /* Different source types, identical converted parameter values. */
    hash = mix(hash, sc_i16((i16)0xf830u, seed));
    hash = mix(hash, sc_change(65530u, seed));
    hash = mix(hash, sc_prune(0, seed));
    if (seed & 1) hash = mix(hash, sc_multi(2000, seed));
    else hash = mix(hash, sc_multi(2001, seed));
    sc_input = 0xa500u | seed;
    return mix(hash, sc_unknown(sc_input));
}
