#ifndef USER_STRESS_H
#define USER_STRESS_H
#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define __edata
#endif

/* No vendor headers, register names, SDK functions or native-ABI annotations. */
#define DECLARE_PAIR(name) u32 name##_clean(u8 seed); u32 name##_messy(u8 seed)
DECLARE_PAIR(fields);
DECLARE_PAIR(parser);
DECLARE_PAIR(ring);
DECLARE_PAIR(bits);
DECLARE_PAIR(callback);
DECLARE_PAIR(giant);
u32 callback_add(u32 x, u8 y);
u32 callback_xor(u32 x, u8 y);
u32 stress_run(u8 kind, u8 seed);
#endif
