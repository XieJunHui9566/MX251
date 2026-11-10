#include "shared-tails.h"
u16 tail_result(u8 kind, u16 input)
{
    u16 result;
    if (kind) {
        result = (input + 3) ^ 0x1234;
        TREG(12) = (u8)result;
    } else {
        result = (input + 7) ^ 0x5678;
        TREG(13) = (u8)result;
    }
    return result;
}

u16 tail_dependencies(u8 kind, u16 input)
{
    if (kind) {
        u16 a = input + 3;
        u16 b = a + 5;
        TREG(14) = (u8)a;
        return b;
    } else {
        u16 a = input + 3;
        u16 b = input + 5;
        TREG(15) = (u8)a;
        return b;
    }
}
