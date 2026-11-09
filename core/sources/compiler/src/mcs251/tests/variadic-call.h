#ifndef VARIADIC_CALL_H
#define VARIADIC_CALL_H
#include "machine-board.h"
extern volatile u16 __data variadic_sample;
u32 variadic_mixed(u8 tag,...);
u32 variadic_samples(u8 tag,...);
u16 variadic_many(u8 count,...);
u16 variadic_mutate(u16 *p,...);
u16 variadic_empty(u16 value,...);
u16 variadic_packed(u8 tag,...);
float variadic_float(u8 tag,...);
const u8 *variadic_pointer(u8 tag,...);
u32 variadic_case(u8 seed);
float variadic_float_caller(u8 tag);
#endif
