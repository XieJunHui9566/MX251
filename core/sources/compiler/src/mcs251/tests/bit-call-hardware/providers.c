/* The existing byte backend implements these ordinary __bit-returning entries. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
volatile __bit saved_bit, flag_state;
volatile __xdata u32 flag_calls;
__bit flag0(void) __reentrant
{
    ++flag_calls;
    return flag_state;
}
__bit flag3(u32 x, u16 y, u8 z) __reentrant
{
    ++flag_calls;
    return ((u8)x ^ (u8)y ^ z) & 1;
}
__bit flag6(u8 x, u32 y, u16 z) __reentrant
{
    ++flag_calls;
    return (x ^ (u8)y ^ (u8)z) & 1;
}
