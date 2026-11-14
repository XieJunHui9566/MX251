#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#define NATIVE __sdcccall(4)
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define __edata
#define NATIVE
#endif

typedef struct {
    u8 lead;
    u16 word;
    u32 dword;
    u8 *pointer;
    u8 tail;
    u8 bytes[5];
} MemberBlock;
extern __edata MemberBlock member_block;
extern volatile __edata MemberBlock member_device;
extern __edata u8 member_targets[2];
extern __edata u16 member_format_bias;
u32 member_format(u8 seed);
void member_mutate(u16 *p, u16 x) NATIVE;
u32 member_work(u8 seed);
u32 member_native(u8 seed) NATIVE;
