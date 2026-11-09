typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;

#ifndef MANY_ABI
#define MANY_ABI 2
#endif
#define ABI __reentrant __sdcccall(MANY_ABI)
#if MANY_ABI == 4
#define MANY_SPACE __xdata
#define MANY_TABLE ram_values
#else
#define MANY_SPACE __code
#define MANY_TABLE code_values
#endif
#define MANY_PARAMETERS \
    u8 a, u16 b, s8 c, s16 d, u32 e, u8 f, u16 g, u32 h, \
    const u8 MANY_SPACE *p, u8 j, u16 k, u32 l, \
    const u8 __xdata *q, s8 n, u16 o, u32 last
#define MANY_VALUES(x) \
    (x), (u16)(0x1234U + (x)), (s8)((x) ^ 0xa5U), (s16)(0x8123U + (x)), \
    0x12345678UL + (x), (u8)((x) + 17U), (u16)(0xfedcU - (x)), \
    0x89abcdefUL ^ (x), MANY_TABLE + ((x) & 3U), (u8)((x) * 3U), \
    (u16)(0x4567U ^ (x)), 0xdeadbeefUL - (x), ram_values + ((x) & 3U), \
    (s8)((x) + 93U), (u16)(0xabcdU + (x)), 0x76543210UL ^ (x)
u32 many_mix(MANY_PARAMETERS) ABI;
u32 legacy_mix(MANY_PARAMETERS) ABI;
extern const u8 __code code_values[4];
extern u8 __xdata ram_values[4];
void compute(u8 x);
extern u32 answers[3];
