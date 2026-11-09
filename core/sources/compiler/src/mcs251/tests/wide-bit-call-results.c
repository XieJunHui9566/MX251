/* ABI2 CY results with stack arguments, live callers and spilled bit values. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
__bit flag0(void) __reentrant;
__bit flag3(u32, u16, u8) __reentrant;
__bit flag6(u8, u32, u16) __reentrant;
extern volatile __bit saved_bit;

u8 bit_call_byte(void) __reentrant { return flag0(); }
u32 bit_call_live(u32 x, u32 y) __reentrant
{
    u8 b = flag6((u8)x, y, 0x1234);
    return x + y + b;
}
u32 bit_call_branch(u32 x) __reentrant
{
    return flag3(x, 0xabcc, 0x36) ? x + 7 : x - 9;
}
u32 bit_call_loop(u32 x) __reentrant
{
    u8 i;
    u32 sum = 0;
    for (i = 0; i < 8; ++i) sum += flag3(x >> i, 0xabcc, 0x36);
    return sum;
}
u32 bit_call_drop(u32 x) __reentrant
{
    flag3(x, 0xabcc, 0x36);
    return x + 1;
}
u32 bit_call_store(u32 x) __reentrant
{
    saved_bit = flag3(x, 0xabcc, 0x36);
    return saved_bit;
}
u32 bit_call_pressure(u32 x) __reentrant
{
    __bit b0 = flag3(x, 0xabcc, 0x36);
    __bit b1 = flag3(x >> 1, 0xabcc, 0x36);
    __bit b2 = flag3(x >> 2, 0xabcc, 0x36);
    __bit b3 = flag3(x >> 3, 0xabcc, 0x36);
    __bit b4 = flag3(x >> 4, 0xabcc, 0x36);
    __bit b5 = flag3(x >> 5, 0xabcc, 0x36);
    __bit b6 = flag3(x >> 6, 0xabcc, 0x36);
    __bit b7 = flag3(x >> 7, 0xabcc, 0x36);
    __bit b8 = flag3(x >> 8, 0xabcc, 0x36);
    __bit b9 = flag3(x >> 9, 0xabcc, 0x36);
    __bit b10 = flag3(x >> 10, 0xabcc, 0x36);
    __bit b11 = flag3(x >> 11, 0xabcc, 0x36);
    __bit b12 = flag3(x >> 12, 0xabcc, 0x36);
    __bit b13 = flag3(x >> 13, 0xabcc, 0x36);
    __bit b14 = flag3(x >> 14, 0xabcc, 0x36);
    __bit b15 = flag3(x >> 15, 0xabcc, 0x36);
    return (u32)b0 | ((u32)b1 << 1) | ((u32)b2 << 2) | ((u32)b3 << 3) |
           ((u32)b4 << 4) | ((u32)b5 << 5) | ((u32)b6 << 6) | ((u32)b7 << 7) |
           ((u32)b8 << 8) | ((u32)b9 << 9) | ((u32)b10 << 10) | ((u32)b11 << 11) |
           ((u32)b12 << 12) | ((u32)b13 << 13) | ((u32)b14 << 14) | ((u32)b15 << 15);
}
