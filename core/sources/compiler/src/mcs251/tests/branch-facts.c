/* Branch facts must describe the current values on every incoming path. */
#include "machine-board.h"

volatile u8 bf_io;
u8 bf_global;

u16 bf_mask(u8 id, u16 flags)
{
    u8 ep = id & 15;
    if (id == 0x81 || id == 1) return flags & ~(1u << ep);
    return flags ^ id;
}

u16 bf_guard(u8 id, u16 flags)
{
    u8 ep = id & 15;
    if (id != 0x81) return flags + id;
    return flags & ~(1u << ep);
}

u16 bf_switch(u8 id)
{
    u8 ep = id & 15;
    switch (id) {
    case 17: return (1u << ep) + 31;
    case 18: case 19: return (1u << ep) + 61;
    case 20: return (1u << ep) + 97;
    case 21: return (1u << ep) + 131;
    case 22: return (1u << ep) + 163;
    case 23: return (1u << ep) + 197;
    case 24: return (1u << ep) + 229;
    case 25: return (1u << ep) + 263;
    case 26: return (1u << ep) + 293;
    case 27: return (1u << ep) + 331;
    case 28: return (1u << ep) + 367;
    case 29: return (1u << ep) + 401;
    case 30: return (1u << ep) + 431;
    case 31: return (1u << ep) + 467;
    case 32: return (1u << ep) + 499;
    case 34: return (1u << ep) + 541;
    case 35: return (1u << ep) + 577;
    case 36: return (1u << ep) + 601;
    default: return (1u << ep) ^ 0x7731;
    }
}

u16 bf_join(u8 id, u8 choose)
{
    u8 saved = id & 15;
    if (choose & 1) id = 0x81;
    else id = 1;
    if (id == 0x81) return (1u << saved) + id;
    return (1u << saved) ^ id;
}

u16 bf_loop(u8 id)
{
    u8 n, saved = id & 15;
    u16 result = 0;
    for (n = 0; n < 5; ++n) {
        if (id == 0x81) result += 1u << saved;
        else result ^= id;
        id += 33;
    }
    n = 3;
    do {
        saved = id & 15;
        id += 17;
        if (id == 0x81) result ^= 1u << saved;
    } while (--n);
    return result;
}

u16 bf_cast(u16 id)
{
    u16 saved = id >> 4;
    if ((u8)id == 0x81) return saved;
    if ((signed char)id == -1) return saved ^ id;
    return id;
}

u32 bf_wide(u32 id)
{
    u32 saved = (id >> 8) ^ 0x003579bdUL;
    if (id == 0x81234567UL) return saved;
    if ((u8)id == 0x67) return saved + id;
    return saved ^ id;
}

u16 bf_signed(signed char id)
{
    u8 ep = (u8)id & 15;
    if (id == -127) return 1u << ep;
    return (u16)(int)id;
}

u16 bf_call_arg(u16 value)
{
    return (value << 3) ^ value ^ 0x5291;
}

void bf_mutate(u8 *value)
{
    *value += 7;
    bf_global += 13;
    bf_io += 11;
}

u16 bf_memory(u8 id)
{
    u8 local = id, snapshot, mask;
    u16 result;
    bf_global = id;
    bf_io = id;
    snapshot = bf_io;
    mask = snapshot & 15;
    bf_mutate(&local);
    if (snapshot == 0x81) result = bf_call_arg(1u << mask);
    else result = local + bf_global;
    if (bf_io == 0x81) result ^= 1u << (bf_io & 15);
    return result;
}

u16 bf_state(u8 id)
{
    u8 state = id & 3, n;
    u16 result = 0x5173;
    for (n = 0; n < 16; ++n) {
        u8 bit = state & 7;
        if (state == 0) result ^= 1u << bit;
        else if (state == 1) result += 1u << bit;
        else if (state == 2) result ^= 1u << bit;
        else result -= 1u << bit;
        state = (state + id + n) & 3;
    }
    return result;
}

u32 bf_case(u8 seed)
{
    u8 i, x;
    u16 word;
    u32 result = 0x13579bdfUL;
    for (i = 0; i < 4; ++i) {
        x = seed * 4 + i;
        word = ((u16)x << 8) | (x ^ 0x91);
        result = (result << 3) ^ result ^ bf_mask(x, word);
        result += bf_guard(x, word);
        result ^= bf_switch(x);
        result += bf_join(x, seed);
        result ^= bf_loop(x);
        result += bf_cast(word);
        result ^= bf_cast(((u16)x << 8) | 0x81);
        result += bf_signed((signed char)x);
        result ^= bf_memory(x);
        result += bf_state(x);
        result ^= bf_wide(((u32)x << 24) | 0x234567UL);
    }
    return result;
}
