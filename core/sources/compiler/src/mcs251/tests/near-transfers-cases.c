typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

u16 near_leaf(u16 x)
{
    return (x ^ 0x1234u) + (u16)((x << 3) | (x >> 13));
}

u16 near_pointer(u16 *p)
{
    return p[0] ^ p[1] ^ p[2];
}

u16 near_local(u16 x)
{
    u16 values[3];
    values[0] = x; values[1] = x + 19; values[2] = x ^ 0x55aa;
    return near_pointer(values) + near_leaf(x);
}

u16 near_recursive(u16 x)
{
    if (x < 3) return x + 17;
    return near_recursive(x - 3) + x;
}

u16 far_stack(u16 x, u16 y, u8 n)
{
    volatile u8 local[4];
    local[0] = x; local[1] = y; local[2] = n; local[3] = x >> 8;
    return near_leaf(x + y) ^ ((u16)local[0] << 8) ^ local[1] ^ local[2] ^ local[3];
}

u16 native_pair(u16 x, u16 y) __sdcccall(4)
{
    return (x + y) ^ 0xa55a;
}

u16 native_stack(u16 x, u16 y, u16 z) __sdcccall(4)
{
    return native_pair(x, y) + z;
}

u16 far_addressed(u16 x)
{
    return (u16)(x * 17u) ^ 0x3c69;
}
u16 (* volatile callback)(u16) = far_addressed;

u32 near_work(u16 seed)
{
    u8 i;
    u16 x, value;
    u32 result = 0x13579bdfUL;
    for (i = 0; i < 8; ++i) {
        x = seed * 1031u + (u16)i * 337u;
        value = near_local(x) ^ far_stack(x, x ^ 0x5678, i);
        value += near_recursive((seed + i) & 31);
        result = (result << 3) ^ result ^ value;
    }
    return result;
}

u32 near_mixed(u16 seed)
{
    u8 i;
    u16 x, value;
    u32 result = 0x2468ace0UL;
    for (i = 0; i < 8; ++i) {
        x = seed * 1877u + (u16)i * 251u;
        value = native_pair(x, x ^ 0x4321);
        value ^= native_stack(x, x ^ 0x5678, seed + i);
        value += callback(x);
        result = (result << 3) ^ result ^ value;
    }
    return result;
}
