typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

volatile u16 old_state;
volatile u8 old_depth;

void old_step()
{
    old_state = (u16)(old_state * 17u) ^ 0x3c69;
}

u16 old_pointer(u16 *p)
{
    return p[0] ^ p[1] ^ p[2];
}

u16 old_read()
{
    u16 values[3];
    u16 x = old_state;
    values[0] = x; values[1] = x + 19; values[2] = x ^ 0x55aa;
    return old_pointer(values) + x;
}

u16 old_recursive()
{
    u16 n = old_depth;
    if (!n) return old_state ^ 0x1234;
    old_depth = n - 1;
    return old_recursive() + n;
}

u32 old_native() __sdcccall(4)
{
    return ((u32)old_state << 16) ^ 0x12345678UL;
}

u8 old_byte()
{
    return old_state ^ 0xa5;
}

u16 old_addressed()
{
    return old_state ^ 0x4321;
}
u16 (* volatile old_callback)(void) = old_addressed;

/* Named K&R parameters remain outside the zero-argument contract. */
u16 old_parameter(x)
u16 x;
{
    return x ^ 0xa55a;
}
