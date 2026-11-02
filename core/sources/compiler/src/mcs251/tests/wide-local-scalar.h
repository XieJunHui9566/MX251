#ifndef WIDE_LOCAL_SCALAR_H
#define WIDE_LOCAL_SCALAR_H
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
typedef signed short s16;
typedef union {u32 u;float f;} Bits;
u32 write8(u8 PTR *p,u32 b) OLD;
u32 write16(s16 PTR *p,u32 b) OLD;
u32 write32(u32 PTR *p,u32 b) OLD;
void write_alias(u32 PTR *p,u32 PTR *q,u32 b) OLD;
void write_volatile(volatile u32 PTR *p,u32 b) OLD;
void write_float(float PTR *p,u32 b) OLD;
u32 c_byte(u32 a,u32 b) ABI;
u32 c_word(u32 a,u32 b) ABI;
u32 c_dword(u32 a,u32 b) ABI;
u32 c_alias(u32 a,u32 b) ABI;
u32 c_float(u32 a,u32 b) ABI;
u32 c_volatile(u32 a,u32 b) ABI;
u32 c_loop(u32 a,u32 b) ABI;
u32 c_recurse(u32 a,u32 b) ABI;
u32 c_frame(u32 a,u32 b) ABI;
u32 c_pressure(u32 a,u32 b) ABI;
#endif
