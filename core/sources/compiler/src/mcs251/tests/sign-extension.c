/* Portable integer conversions; no SDK or peripheral dependency. */
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;

u32 unsigned_pipeline(u8 seed)
{
    u32 result = 0x13579bdfUL;
    u16 i;
    for (i = 0; i < 1024; ++i) {
        s16 value = (s16)(((u16)seed << 10) | i);
        result = (result ^ (u32)(s32)value) * 16777619UL;
        result ^= (u32)(s32)(s8)(value & 255);
    }
    return result;
}

s32 mixed_width_sum(s16 a, s16 b, s8 c, s32 previous)
{
    return (s32)a + (s32)b + (s32)c + (previous & 0x7fffL);
}

u32 signed_calibration(u8 seed)
{
    s16 a = (s16)(0x8000u + (u16)seed * 997u);
    s16 b = (s16)(0x7fffu - (u16)seed * 419u);
    s8 c = (s8)((u16)seed * 37u);
    s32 result = seed;
    u16 i;
    for (i = 0; i < 128; ++i) {
        result = mixed_width_sum(a, b, c, result);
        a = (s16)((u16)a + 1237u);
        b = (s16)((u16)b - 173u);
    }
    return (u32)result;
}
