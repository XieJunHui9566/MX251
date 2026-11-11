typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int i16;

u16 word_unsigned(u16 x)
{
    u16 result = 0;
    if (x < 30u) result |= 1u;
    if (x <= 1000u) result |= 2u;
    if (x > 0x3fffu) result |= 4u;
    if (x >= 0x8000u) result |= 8u;
    if (x == 0xffffu) result |= 16u;
    if (x != 0x8001u) result |= 32u;
    result |= (u16)(x < 0x1234u) << 6;
    result |= (u16)(x >= 0x7fffu) << 7;
    return result;
}

u16 word_signed(i16 x)
{
    u16 result = 0;
    if (x < -1234) result |= 1u;
    if (x <= -1) result |= 2u;
    if (x > 1234) result |= 4u;
    if (x >= 32767) result |= 8u;
    if (x == (-32767 - 1)) result |= 16u;
    if (x != -1234) result |= 32u;
    result |= (u16)(x < 30) << 6;
    result |= (u16)(x >= 1000) << 7;
    return result;
}

volatile __data u16 word_sample;
u8 word_volatile(void) { return word_sample < 1000u; }
u8 word_register(u16 x, u16 y) { return x < y; }
u8 word_dword(unsigned long x) { return x < 0x12345678UL; }
