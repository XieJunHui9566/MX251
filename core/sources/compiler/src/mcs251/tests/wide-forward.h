#ifndef WIDE_FORWARD_H
#define WIDE_FORWARD_H
#if defined(__SDCC)
typedef unsigned long u32;
#define OLD __reentrant
#define ABI __reentrant __sdcccall(4)
#define PTR __xdata
#elif defined(__C251__)
typedef unsigned long u32;
#define OLD reentrant
#define ABI reentrant
#define PTR far
#else
#include <stdint.h>
typedef uint32_t u32;
#define OLD
#define ABI
#define PTR
#endif
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed char s8;
typedef signed short s16;
typedef union {u32 u;float f;} Bits;

s8 ret_byte(s8 a,u32 b) OLD;
s16 ret_word(s16 a,u32 b) OLD;
u32 ret_dword(u32 a,u32 b) OLD;
float ret_float(float a,float b,u8 choose) OLD;
u32 PTR *ret_pointer(u32 PTR *p,u32 PTR *q,u8 choose) OLD;
u32 touch(u32 PTR *p,u32 v) OLD;
u32 touch_volatile(volatile u32 PTR *p,u32 v) OLD;
u32 ret_many(u8 a,u16 b,u32 c,s8 d,u8 PTR *e,s16 f,u32 g,u8 h) OLD;
u32 native_ret(u32 a,u32 b) ABI;
u32 c_pair(u32 a,u32 b) OLD;
u32 c_native(u32 a,u32 b) OLD;
u32 c_recur(u32 a,u32 b) OLD;
u32 c_alias(u32 a,u32 b) OLD;
u32 c_many(u32 a,u32 b) OLD;
u32 c_pointer(u32 a,u32 b) OLD;
u32 c_float(u32 a,u32 b) OLD;
u32 c_pressure(u32 a,u32 b) OLD;
u32 c_frame(u32 a,u32 b) OLD;
u32 c_callback(u32 a,u32 b) OLD;
#endif
