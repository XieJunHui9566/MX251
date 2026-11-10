/* Branch predicates with retained sources, byte loads and wide byte lanes. */
#include "machine-board.h"
#define NATIVE __sdcccall(4)
volatile __data u8 pb_byte;
volatile __data u16 pb_word;
volatile __data u32 pb_long;
__sbit __at(0x97) pb_port_bit;
volatile __bit pb_flag;

u32 pb_bits(u32 x) NATIVE
{
    u32 result = x;
    if (x & 1UL) result ^= 0x81731943UL;
    if (x & 2UL) result += 0x12345679UL;
    if (x & 4UL) result ^= 0x8193abcdUL;
    if (x & 8UL) result += 0x2468abcdUL;
    if (x & 16UL) result ^= 0x1098fedcUL;
    if (x & 32UL) result += 0x76543211UL;
    if (x & 64UL) result ^= 0x93abc721UL;
    if (x & 128UL) result += 0x30417261UL;
    if (x & 0x8000UL) result ^= 0x10293847UL;
    if (x & 0x10000UL) result += 0xabcdef01UL;
    if (x & 0x80000000UL) result ^= 0x91a2b3c4UL;
    if (x & 0x00f00000UL) result += 0x27182819UL;
    if (!(x & 0xff000000UL)) result ^= 0xfedcba98UL;
    return result ^ x;
}

u32 pb_loads(u8 x, u8 y) NATIVE
{
    volatile u8 local = x;
    u32 result = y;
    pb_byte = x;
    if (pb_byte & 0x80) result += 0x12345UL;
    if (!(pb_byte & 0x24)) result ^= 0xabcdeUL;
    if (local & 0x40) result += 0x98765UL;
    pb_word = ((u16)x << 8) | y;
    if (pb_word & 0x1000) result ^= 0x773355UL;
    pb_long = ((u32)x << 24) | ((u32)y << 16) | 0x4321UL;
    if (pb_long & 0x80000000UL) result += 0x112233UL;
    pb_flag = x & 1;
    if (pb_flag) result ^= 0x573199UL;
    if (!pb_flag) result += 0x319757UL;
    /* Several byte lanes and sign extension still use the full value. */
    if (pb_long & 0x00800001UL) result ^= 0x772191UL;
    if ((signed char)x & 0x80000000UL) result += 0x111777UL;
    /* The masked value remains live after the branch. */
    y &= 0xa5;
    if (y) result ^= 0x9715UL;
    return result + y + local;
}

u32 pb_case(u8 seed)
{
    u8 i, x;
    u32 result = 0x13579bdfUL, wide;
    for (i = 0; i < 16; ++i) {
        x = seed * 4 + i;
        wide = ((u32)x << 24) | ((u32)(x ^ 0x91) << 16) |
               ((u16)(x ^ 0x37) << 8) | (x ^ 0xa6);
        result = (result << 3) ^ result ^ pb_bits(wide) ^ pb_loads(x, x ^ 0xc3);
    }
    return result;
}

/* Compile-only SFR-bit polling; the hardware fixture does not call these. */
u8 pb_bit_poll(u8 x) NATIVE
{
    while (pb_port_bit) {}
    if (!pb_port_bit) x ^= 0x57;
    return x;
}

void pb_far(u8 x) NATIVE
{
    while (x & 0x80) {
        pb_byte = 1; pb_byte = 2; pb_byte = 3; pb_byte = 4;
        pb_byte = 5; pb_byte = 6; pb_byte = 7; pb_byte = 8;
        pb_byte = 9; pb_byte = 10; pb_byte = 11; pb_byte = 12;
        pb_byte = 13; pb_byte = 14; pb_byte = 15; pb_byte = 16;
        pb_byte = 17; pb_byte = 18; pb_byte = 19; pb_byte = 20;
        pb_byte = 21; pb_byte = 22; pb_byte = 23; pb_byte = 24;
        pb_byte = 25; pb_byte = 26; pb_byte = 27; pb_byte = 28;
        pb_byte = 29; pb_byte = 30; pb_byte = 31; pb_byte = 32;
        pb_byte = 33; pb_byte = 34; pb_byte = 35; pb_byte = 36;
        x = pb_byte;
    }
}
