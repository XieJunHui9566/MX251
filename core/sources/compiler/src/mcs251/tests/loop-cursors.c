#include "loop-cursors.h"

const u8 __code cursor_a[32] = {
    7, 11, 19, 251, 128, 0, 99, 37, 62, 41, 83, 93, 15, 214, 38, 76,
    87, 211, 129, 51, 28, 10, 199, 137, 162, 141, 183, 193, 115, 14, 138, 176
};
const u8 __code cursor_b[32] = {
    131, 22, 241, 17, 79, 182, 193, 4, 58, 96, 166, 254, 145, 23, 0, 6,
    31, 122, 41, 117, 179, 82, 93, 104, 158, 196, 66, 154, 45, 123, 100, 106
};
u8 __xdata cursor_x[260], cursor_y[260];

/* Four streams, both index representations, unchanged counter exit value. */
u8 cursor_copy(void)
{
    u8 i;
    for (i = 0; i < 32; ++i) {
        cursor_x[i] = cursor_a[i];
        cursor_y[i] = cursor_b[i];
    }
    return i;
}

/* A 16-bit counter crosses a byte boundary. The second access observes the
 * first write, so retaining memory order is part of the result. */
u16 cursor_fill(u8 seed)
{
    u16 i;
    for (i = 0; i < 260; ++i) {
        cursor_x[i] = (u8)i ^ seed;
        cursor_y[i] = cursor_x[i] + 3;
    }
    return i;
}

u32 cursor_case(u8 kind, u8 seed)
{
    u16 i, count;
    u32 result = 0x13579bdfUL ^ seed;
    count = kind ? cursor_fill(seed) : cursor_copy();
    for (i = 0; i < count; ++i) {
        result = (result << 5) ^ result ^ cursor_x[i];
        result = (result << 5) ^ result ^ cursor_y[i];
    }
    return result ^ count;
}
