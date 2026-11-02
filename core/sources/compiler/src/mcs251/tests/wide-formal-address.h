#if defined(__SDCC)
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
#define OLD __reentrant
#define PTR __xdata
#elif defined(__C251__)
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
#define ABI reentrant
#define OLD reentrant
#define PTR far
#else
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef uint32_t u32;
#define ABI
#define OLD
#define PTR
#endif
typedef union {u32 u;float f;} Bits;
u32 write8(u8 PTR *,u32) OLD;
u32 write16(s16 PTR *,u32) OLD;
u32 write_u16(u16 PTR *,u32) OLD;
u32 write32(u32 PTR *,u32) OLD;
void write_alias(u32 PTR *,u32 PTR *,u32) OLD;
void write_float(float PTR *,u32) OLD;
u32 read_float(float PTR *) OLD;
u32 c_first(u32,u32) ABI;u32 c_second(u32,u32) ABI;
u32 c_third(u32,u32) ABI;u32 c_mixed(u32,u32) ABI;
u32 c_alias(u32,u32) ABI;u32 c_float(u32,u32) ABI;
u32 c_loop(u32,u32) ABI;u32 c_recurse(u32,u32) ABI;
u32 c_frame(u32,u32) ABI;u32 c_pressure(u32,u32) ABI;
