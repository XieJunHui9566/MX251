#include "pointer-stride.h"

u8 stride_signed(const u8 *p, int i)
{
    u8 value = *(p + i * 2);
    return value;
}

u8 stride_unsigned(const u8 *p, u16 i)
{
    return *(p + i * 2);
}

u8 stride_nested(const u8 *p, int i)
{
    return *(p + (i + 1) * 3);
}

u8 stride_subscript(const u8 *p, int i)
{
    return p[i * 2];
}

u8 stride_volatile(const volatile u8 *p, int i)
{
    return *(p + i * 2);
}

u8 stride_word(const u16 *p, int i)
{
    return (u8)*(p + i * 2);
}

u8 stride_wrapped(const u8 *p, int i)
{
    return *(p + (u8)(i * 2));
}

void stride_store(u8 *p, int i, u8 value)
{
    *(p + i * 2) = value;
}
