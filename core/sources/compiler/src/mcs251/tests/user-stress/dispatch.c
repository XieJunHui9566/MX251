#include "stress.h"
/* Kept in a separate translation unit, as in a real user library. */
u32 callback_add(u32 x, u8 y) { return x + (u32)y * 257UL + 0x1234UL; }
u32 callback_xor(u32 x, u8 y) { return (x << 3) ^ (x >> 7) ^ y; }
u32 stress_run(u8 kind, u8 seed)
{
    switch (kind) {
    case 0: return fields_clean(seed); case 1: return fields_messy(seed);
    case 2: return parser_clean(seed); case 3: return parser_messy(seed);
    case 4: return ring_clean(seed); case 5: return ring_messy(seed);
    case 6: return bits_clean(seed); case 7: return bits_messy(seed);
    case 8: return callback_clean(seed); case 9: return callback_messy(seed);
    case 10: return giant_clean(seed); default: return giant_messy(seed);
    }
}
