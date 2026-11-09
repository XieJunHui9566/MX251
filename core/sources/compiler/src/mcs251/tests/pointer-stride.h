#include "machine-board.h"

u8 stride_signed(const u8 *p, int i);
u8 stride_unsigned(const u8 *p, u16 i);
u8 stride_nested(const u8 *p, int i);
u8 stride_subscript(const u8 *p, int i);
u8 stride_volatile(const volatile u8 *p, int i);
u8 stride_word(const u16 *p, int i);
u8 stride_wrapped(const u8 *p, int i);
u8 stride_cast_signed(const u8 *p, long i);
u8 stride_cast_unsigned(const u8 *p, u32 i);
u8 stride_cast_subtract(const u8 *p, long i);
u8 stride_cast_pair(const u8 *p, long i);
u8 stride_code(const u8 __code *p, int i);
u8 stride_merge(const u8 *p, int a, long b, u8 choose);
u8 stride_byte(const u8 *p, signed char i);
void stride_store(u8 *p, int i, u8 value);
void stride_init(void);
u32 stride_case(u8 seed);
