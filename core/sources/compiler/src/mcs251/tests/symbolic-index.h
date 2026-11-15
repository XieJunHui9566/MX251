#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 4
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#define RAM __xdata
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
#define ABI
#define RAM
#endif

extern RAM u8 si_bytes[256];
extern RAM u16 si_words[128];
extern RAM u32 si_longs[64];
extern volatile RAM u8 si_volatile[64];
u16 si_touch(u16 index,u8 seed) ABI;
void si_init(u8 seed) ABI;
u32 si_byte(u8 seed) ABI;
u32 si_word(u8 seed) ABI;
u32 si_long(u8 seed) ABI;
u32 si_offset(u8 seed) ABI;
u32 si_branch(u8 seed) ABI;
u32 si_negative(u8 seed) ABI;
u32 si_dynamic(u8 *p,u8 seed) ABI;
u32 si_ordered(u8 seed) ABI;
u32 stress_run(u8 kind,u8 seed);
