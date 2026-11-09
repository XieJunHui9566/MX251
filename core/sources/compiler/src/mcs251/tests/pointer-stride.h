#include "machine-board.h"

u8 stride_signed(const u8 *p, int i);
u8 stride_unsigned(const u8 *p, u16 i);
u8 stride_nested(const u8 *p, int i);
u8 stride_subscript(const u8 *p, int i);
u8 stride_volatile(const volatile u8 *p, int i);
u8 stride_word(const u16 *p, int i);
u8 stride_wrapped(const u8 *p, int i);
void stride_store(u8 *p, int i, u8 value);
void stride_init(void);
u32 stride_case(u8 seed);
