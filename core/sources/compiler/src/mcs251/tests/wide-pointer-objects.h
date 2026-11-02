#if defined(__SDCC)
typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
#define OLD __reentrant
#define PTR __xdata
#define UPTR u32
#elif defined(__C251__)
typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI reentrant
#define OLD reentrant
#define PTR far
#define UPTR u32
#else
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define ABI
#define OLD
#define PTR
#define UPTR uintptr_t
#endif
typedef u32 PTR *P32;
typedef struct {u8 before;P32 pointer;u8 after;} Guard;
u32 swap_pointer(P32 PTR *,P32,u32) OLD;
u32 read_pointer(P32 PTR *) OLD;
void set_bits(P32 PTR *,u32) OLD;
u32 c_local(u32,u32) ABI;u32 c_first(u32,u32) ABI;
u32 c_second(u32,u32) ABI;u32 c_third(u32,u32) ABI;
u32 c_mixed(u32,u32) ABI;u32 c_alias(u32,u32) ABI;
u32 c_volatile(u32,u32) ABI;u32 c_loop(u32,u32) ABI;
u32 c_bits(u32,u32) ABI;u32 c_pressure(u32,u32) ABI;
