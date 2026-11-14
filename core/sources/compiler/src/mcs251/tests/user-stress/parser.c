#include "stress.h"
/* Byte stream decoder with sync, length, payload and checksum states. */
u32 parser_clean(u8 seed)
{
    u8 i, state = 0, left = 0, check = 0;
    u32 result = seed;
    for (i = 0; i < 64; ++i) {
        u8 ch = (i % 9 == 0) ? 0xa5 : (u8)(seed + 13U * i);
        switch (state) {
        case 0: if (ch == 0xa5) state = 1; break;
        case 1: left = (ch & 7) + 1; check = ch; state = 2; break;
        case 2:
            check ^= ch; result = (result << 3) ^ ch ^ (result >> 5);
            if (--left == 0) state = 3;
            break;
        default: result += (check == ch) ? 0x10001UL : 3; state = 0; break;
        }
    }
    return result ^ ((u32)state << 24) ^ ((u16)check << 8) ^ left;
}

u32 parser_messy(u8 seed)
{
    u8 i = 0, state = 0, left = 0, check = 0, ch;
    u32 result = seed;
again:
    ch = (i % 9 == 0) ? 0xa5 : (u8)(seed + 13U * i);
    if (state != 0) goto other;
    if (ch != 0xa5) goto next;
    state = 1; goto next;
other:
    if (state == 1) {
        left = (ch & 7) + 1; check = ch; state = 2;
    } else if (state != 2) {
        if (check != ch) result += 3; else result += 0x10001UL;
        state = 0;
    } else {
        check = check ^ ch;
        result = (result << 3) ^ ch ^ (result >> 5);
        left -= 1;
        if (!left) state = 3;
    }
next:
    if (++i != 64) goto again;
    return result ^ ((u32)state << 24) ^ ((u16)check << 8) ^ left;
}
