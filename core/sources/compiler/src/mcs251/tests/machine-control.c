/* Portable byte-control and private-call workload; no hardware registers. */
#include "machine-board.h"
static u8 _swap(u8 value)
{
    return (value << 1) | (value >> 7);
}
static void edit(u16 *a,u16 *b)
{
    *a = *a + 7;
    *b = *b ^ *a;
}
u32 control(u8 seed)
{
    u8 i, state = seed & 3, octet;
    u16 length = 0x1234, status = 0;
    u32 result = 0x13579bdfUL;
    if (seed & 1) edit(&length,&length);
    else {
        u16 status = 0x5678;
        edit(&length,&status);
        result ^= status;
    }
    for (i = 0; i < 32; ++i) {
        octet = _swap(seed + i);
        if (state == 0) {
            length += octet;
            status = octet & 0x80 ? 0x101 : 0x202;
        } else if (state == 1) {
            length ^= ((u16)octet << 8) | i;
            status += 3;
        } else if (state == 2) {
            if (octet < 128) status ^= length;
            else length -= octet;
        } else {
            status = (status & 0xff00) | octet;
            length += status;
        }
        result = (result << 3) ^ length ^ ((u32)status << 16) ^ result;
        state = (state + 1) & 3;
    }
    return result;
}
