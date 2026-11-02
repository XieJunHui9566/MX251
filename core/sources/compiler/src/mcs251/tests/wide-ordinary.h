#ifndef WIDE_ORDINARY_H
#define WIDE_ORDINARY_H
#if defined(__SDCC)
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
#define OLD __reentrant
#define PTR __xdata
#elif defined(__C251__)
typedef unsigned long u32;
#define ABI reentrant
#define OLD reentrant
#define PTR far
#else
#include <stdint.h>
typedef uint32_t u32;
#define ABI
#define OLD
#define PTR
#endif
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed char s8;
typedef signed short s16;
typedef union {u32 u;float f;} Bits;
u32 zero(void) OLD;
s8 byte_value(s8 a,u32 b) OLD;
s16 word_value(s16 a,u8 b) OLD;
u32 dword_value(u32 a,u32 b) OLD;
u8 PTR *pointer_value(u8 PTR *a,u32 b,u8 PTR *c) OLD;
float float_value(float a,float b,u8 choose) OLD;
u32 many(u8 a,u16 b,u32 c,s8 d,u8 PTR *e,s16 f,u32 g,u8 h) OLD;
void store_value(u32 PTR *p,u32 a,u32 b) OLD;
u32 chain(u32 a,u32 b) OLD;
u32 native_leaf(u32 a,u32 b) ABI;
float float_bridge(float a,float b,u8 choose) ABI;
u32 c_zero(u32 a,u32 b) ABI;
u32 c_byte(u32 a,u32 b) ABI;
u32 c_word(u32 a,u32 b) ABI;
u32 c_dword(u32 a,u32 b) ABI;
u32 c_pointer(u32 a,u32 b) ABI;
u32 c_float(u32 a,u32 b) ABI;
u32 c_many(u32 a,u32 b) ABI;
u32 c_store(u32 a,u32 b) ABI;
u32 c_chain(u32 a,u32 b) ABI;
u32 c_pressure(u32 a,u32 b) ABI;
#endif
