#if defined(__SDCC)
typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define OLD __reentrant
#define PTR __xdata
#define UPTR u32
#elif defined(__C251__)
typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define OLD reentrant
#define PTR far
#define UPTR u32
#else
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define OLD
#define PTR
#define UPTR uintptr_t
#endif
typedef u32 PTR *P32;
u32 leaf_pointer(P32,P32,u32) OLD;
u32 leaf_bits(P32,P32,u32) OLD;
void leaf_frame(P32,u32,u32) OLD;
float leaf_negative_zero(void) OLD;
u32 c_first(u32,u32) OLD;u32 c_second(u32,u32) OLD;
u32 c_alias(u32,u32) OLD;u32 c_local(u32,u32) OLD;
u32 c_ptr(u32,u32) OLD;u32 c_bits(u32,u32) OLD;
u32 c_unused(u32,u32) OLD;u32 c_frame(u32,u32) OLD;
u32 c_float(u32,u32) OLD;u32 c_pressure(u32,u32) OLD;
