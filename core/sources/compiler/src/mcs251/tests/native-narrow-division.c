#include "native-narrow-division.h"

div_u8 div_q8(div_u8 a, div_u8 b) DIV_ABI { return a / b; }
div_u8 div_r8(div_u8 a, div_u8 b) DIV_ABI { return a % b; }
div_u16 div_q16(div_u16 a, div_u16 b) DIV_ABI { return a / b; }
div_u16 div_r16(div_u16 a, div_u16 b) DIV_ABI { return a % b; }
div_u8 div_narrow_q(div_u16 a, div_u16 b) DIV_ABI { return (div_u8)(a / b); }
div_u8 div_narrow_r(div_u16 a, div_u16 b) DIV_ABI { return (div_u8)(a % b); }
div_u16 div_mixed(div_u16 a, div_u8 b) DIV_ABI { return a / b + a % b; }
div_u16 div_byte_word(div_u8 a, div_u16 b) DIV_ABI { return a / b + a % b; }

/* Includes the byte division exposed by IR narrowing at the last digits. */
div_u32 div_decimal(div_u16 a) DIV_ABI
{
    div_u8 d0 = a / 10000u, d1 = (a % 10000u) / 1000u;
    div_u8 d2 = (a % 1000u) / 100u, d3 = (a % 100u) / 10u, d4 = a % 10u;
    return ((div_u32)d0 << 16) | ((div_u16)d1 << 12) |
           ((div_u16)d2 << 8) | ((div_u16)d3 << 4) | d4;
}

/* Simultaneous quotients, remainders and incoming stack arguments. */
div_u16 div_live(div_u16 a, div_u16 b, div_u16 c, div_u16 d,
                div_u16 e, div_u16 f, div_u16 g, div_u16 h) DIV_ABI
{
    div_u16 q0 = a / b, q1 = c / b, q2 = d / b, q3 = e / b;
    div_u16 q4 = f / b, q5 = g / b, q6 = h / b;
    div_u16 r0 = a % b, r1 = c % b, r2 = d % b, r3 = e % b;
    return (q0 << 1) ^ (q1 << 2) ^ (q2 << 3) ^ (q3 << 4) ^
           (q4 << 5) ^ (q5 << 6) ^ q6 ^ r0 ^ r1 ^ r2 ^ r3;
}

#ifdef __SDCC
volatile __data div_u16 div_sample;
#else
volatile div_u16 data div_sample;
#endif
div_u16 div_volatile(div_u16 a, div_u16 b) DIV_ABI
{
    div_u16 q, r;
    div_sample = a;
    q = div_sample / b;
    div_sample = a ^ 0xa55au;
    r = div_sample % b;
    return q ^ r;
}
