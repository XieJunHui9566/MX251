/* Portable compiler workloads: no peripheral or SDK dependency. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

u32 unsigned_pipeline(u8 seed)
{
    u32 value = 0xf1234567UL ^ ((u32)seed * 0x10001UL);
    u32 result = 0x13579bdfUL;
    u16 i;
    for (i = 0; i < 12; ++i) {
        u32 divisor = (u32)(seed + i + 3) * 0x101UL + 1;
        u32 quotient = value / divisor;
        u32 remainder = value % divisor;
        result ^= quotient + remainder;
        value = (value ^ result) * (0x1021UL + i) + quotient;
    }
    return result ^ value;
}

u32 signed_calibration(u8 seed)
{
    long value = -123456789L + (long)seed * 76543L;
    long bias = 314159L - (long)seed * 193L;
    u32 result = 0x2468ace0UL;
    u16 i;
    for (i = 0; i < 16; ++i) {
        long divisor = (long)(17 + seed + i);
        long quotient, remainder;
        if (i & 1) divisor = -divisor;
        quotient = value / divisor;
        remainder = value % divisor;
        result = (result << 3) ^ (u32)(quotient + bias) ^ (u32)remainder;
        /* Bounds for seed 0..63 keep the signed recurrence within int32. */
        value = quotient * 17L + remainder * 31L - bias;
        bias += remainder;
    }
    return result ^ (u32)value ^ (u32)bias;
}
