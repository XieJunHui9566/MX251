typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifndef FN
#define FN(name) name
#endif
#ifndef ABI
#define ABI
#endif
__sbit __at(0xd7) ArithmeticCarry;
static volatile __bit serial_bit;
static volatile __bit serial_clock;

u8 FN(packet)(u8 value) ABI
{
    u8 i, observed = 0;
    for (i = 0; i < 8; ++i) {
        value <<= 1;
        serial_bit = ArithmeticCarry;
        serial_clock = 1;
        serial_clock = 0;
        observed = (observed << 1) | serial_bit;
    }
    return observed;
}

#define SHIFT_CASE(name,type,operation) \
u32 FN(name)(type value) ABI { \
    u8 carry; \
    value operation 1; \
    carry = ArithmeticCarry; \
    return (u32)value ^ (carry ? 0x9e3779b9UL : 0); \
}
SHIFT_CASE(left8,u8,<<=)
SHIFT_CASE(right8,u8,>>=)
SHIFT_CASE(left16,u16,<<=)
SHIFT_CASE(right16,u16,>>=)
SHIFT_CASE(left32,u32,<<=)
SHIFT_CASE(right32,u32,>>=)

static const __code u32 edges[] = {
    0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff,
    0x7fffffffUL, 0x80000000UL, 0xffffffffUL, 0x55555555UL,
    0xaaaaaaaaUL, 0x80000001UL, 0xfffffffeUL, 0x12345678UL
};

u32 FN(work)(u8 seed) ABI
{
    u8 i, octet;
    u32 result = 0x13579bdfUL;
    u32 value = edges[seed & 15] ^ ((u32)(seed >> 4) * 0x01010101UL);
    for (i = 0; i < 4; ++i) {
        octet = seed + (u8)(i * 64);
        result = (result << 3) ^ FN(packet)(octet);
        result = (result << 3) ^ FN(left8)(octet);
        result = (result << 3) ^ FN(right8)(octet);
    }
    result = (result << 3) ^ FN(left16)((u16)value);
    result = (result << 3) ^ FN(right16)((u16)value);
    result = (result << 3) ^ FN(left32)(value);
    result = (result << 3) ^ FN(right32)(value);
    return result;
}
