typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 4
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
u16 negate_word(u16 value) ABI;
u8 negate_byte(u8 value) ABI;
u16 negate_loop(u16 value,u8 count) ABI;
u8 byte_loop(u8 value,u8 count) ABI;
u16 mixed_calls(u16 value,u8 count,u16 keep) ABI;
u16 divide_negate(u32 divisor) ABI;
u16 ordinary_step(u16 value);
u32 choice_case(u8 kind,u8 seed);
