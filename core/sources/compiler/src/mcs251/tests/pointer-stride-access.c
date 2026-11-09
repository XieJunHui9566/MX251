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

u8 stride_cast_signed(const u8 *p, long i)
{
    return *(p + (_BitInt(24))i);
}

u8 stride_cast_unsigned(const u8 *p, u32 i)
{
    return *(p + (unsigned _BitInt(24))i);
}

u8 stride_cast_subtract(const u8 *p, long i)
{
    return *(p - (_BitInt(24))i);
}

u8 stride_cast_pair(const u8 *p, long i)
{
    _BitInt(24) offset = (_BitInt(24))i;
    return *(p + offset) ^ *(p - offset);
}

u8 stride_code(const u8 __code *p, int i)
{
    return *(p + i * 2);
}

u8 stride_merge(const u8 *p, int a, long b, u8 choose)
{
    _BitInt(24) offset;
    if (choose) offset = a;
    else offset = b;
    return *(p + offset);
}

u8 stride_byte(const u8 *p, signed char i)
{
    return *(p + i);
}

void stride_store(u8 *p, int i, u8 value)
{
    *(p + i * 2) = value;
}
