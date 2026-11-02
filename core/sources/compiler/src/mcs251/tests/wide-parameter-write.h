#ifndef WIDE_PARAMETER_WRITE_H
#define WIDE_PARAMETER_WRITE_H
#if defined(__SDCC)
typedef unsigned long u32;
typedef unsigned long uaddr;
#define ABI __reentrant __sdcccall(4)
#define OLD __reentrant
#define PTR __xdata
#elif defined(__C251__)
typedef unsigned long u32;
typedef unsigned long uaddr;
#define ABI reentrant
#define OLD reentrant
#define PTR far
#else
#include <stdint.h>
typedef uint32_t u32;
typedef uintptr_t uaddr;
#define ABI
#define OLD
#define PTR
#endif
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed char s8;
typedef signed short s16;
typedef union {u32 u;float f;} Bits;
u32 legacy_value(u32 a,u32 b) OLD;
u32 p_byte(u32 a,u32 b,u8 c,u32 guard) ABI;
u32 p_word(u32 a,u32 b,s16 c,u8 guard) ABI;
u32 p_dword(u32 a,u32 b,u8 guard,u32 c,u16 tail) ABI;
u32 p_pointer(u32 a,u32 b,u8 left,u8 PTR *p,u16 right,u8 PTR *q) ABI;
u32 p_address(u32 a,u32 b,u8 left,u8 PTR *p,u16 right,u8 PTR *q,u32 guard,u8 last) ABI;
float p_float(u32 a,u32 b,u8 guard,float x,u16 tail,float y) ABI;
u32 p_eight(u32 a,u32 b,u8 c,u16 d,u32 e,u8 f,u16 g,u32 h) ABI;
u32 p_loop(u32 a,u32 b,u8 n,u32 c,u16 d) ABI;
u32 p_recurse(u32 a,u32 b,u8 n,u32 c,u16 guard) ABI;
u32 p_frame(u32 a,u32 b,u8 guard,u32 c,u16 tail) ABI;
u32 p_pressure(u32 a,u32 b,u8 guard,u32 c,u16 tail) ABI;
u32 c_byte(u32 a,u32 b) ABI;
u32 c_word(u32 a,u32 b) ABI;
u32 c_dword(u32 a,u32 b) ABI;
u32 c_pointer(u32 a,u32 b) ABI;
u32 c_float(u32 a,u32 b) ABI;
u32 c_eight(u32 a,u32 b) ABI;
u32 c_loop(u32 a,u32 b) ABI;
u32 c_recurse(u32 a,u32 b) ABI;
u32 c_frame(u32 a,u32 b) ABI;
u32 c_pressure(u32 a,u32 b) ABI;
#endif
