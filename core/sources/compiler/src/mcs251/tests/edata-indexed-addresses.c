/* Dynamic offsets into fixed objects, independent of SDK and target names. */
#ifdef __SDCC
typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;
typedef signed int s16;typedef unsigned long u32;typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;
typedef uint32_t u32;typedef int32_t s32;
#define ABI
#define __edata
#define __xdata
#define __code
#endif

static u8 __edata bytes[64],other[64];
static u16 __edata words[32];
static u32 __edata dwords[16];
static volatile u16 __edata vwords[32];
static volatile u32 __edata vdwords[16];
static volatile u8 __edata index_sample;
typedef struct {u8 tag;u8 data[64];} Record;
static Record __edata record;
static u8 __xdata far_bytes[64];
static const u8 __code code_bytes[8]={1,3,7,11,17,23,29,31};
static u8 *volatile cursor;

u32 positive_byte(u32 x,u32 y) ABI {
    u8 i=(u8)y&63;bytes[i]=(u8)x;bytes[i^1]=(u8)y;index_sample=i;return bytes[index_sample]^x;
}
u32 positive_word(u32 x,u32 y) ABI {
    u16 i=(u16)y&31;words[i]=(u16)x;words[i^1]=(u16)y;index_sample=(u8)i;return words[index_sample]^x;
}
u32 positive_dword(u32 x,u32 y) ABI {
    u32 i=y&15;dwords[i]=x;dwords[i^1]=y;index_sample=(u8)i;return dwords[index_sample]^(x>>16);
}
u32 positive_signed(u32 x,u32 y) ABI {
    u8 *p=bytes+32;s8 i=(s8)((s16)(y&31)-16);
    p[i]=(u8)x;return p[i]^x;
}
u32 positive_subtract(u32 x,u32 y) ABI {
    u8 *p=bytes+32;u16 i=(u16)y&31;
    *(p-i)=(u8)x;return *(p-i)^x;
}
u32 positive_volatile_word(u32 x,u32 y) ABI {
    u16 i=(u16)y&31;vwords[i]=(u16)x;return vwords[i]^x;
}
u32 positive_volatile_dword(u32 x,u32 y) ABI {
    u32 i=y&15;vdwords[i]=x;return vdwords[i]^(x>>16);
}
u32 positive_sampled_index(u32 x,u32 y) ABI {
    index_sample=(u8)y&63;bytes[index_sample]=(u8)x;
    return bytes[index_sample]^x;
}
u32 positive_member(u32 x,u32 y) ABI {
    u8 i=(u8)y&63;record.tag=(u8)y;record.data[i]=(u8)x;
    return record.data[i]^((u32)record.tag<<24)^x;
}
u32 positive_loop(u32 x,u32 y) ABI {
    u8 i;u32 result=0;
    for(i=0;i<16;++i)record.data[i]=(u8)(x+17U*i);
    for(i=0;i<16;++i)result=(result<<1)^record.data[i];
    return result^x^y;
}
u8 negative_parameter(u8 *p,u8 i) ABI {return p[i&63];}
u8 negative_merge(u8 x,u8 i) ABI {
    u8 *p;if(x&1)p=bytes;else p=other;return p[i&63];
}
u8 negative_pointer(u8 i) ABI {return cursor[i&63];}
u8 negative_far(u8 i) ABI {return far_bytes[i&63];}
u8 negative_code(u8 i) ABI {return code_bytes[i&7];}
u8 negative_integer(u16 address,u8 i) ABI {return ((u8 *)(u32)address)[i&63];}

u32 shift_masked(u32 x,u32 y) ABI {
    return (x<<(u8)(y&31))^(u32)((s32)x>>(u8)((y>>8)&31));
}
u32 shift_byte(u32 x,u32 y) ABI {
    u8 count=(u8)y&7;
    return (u8)((u8)x<<count)^(u8)((s8)x>>count)^x;
}

u32 stress_run(u8 kind,u8 seed) {
    u8 j;u32 result=0,state=0x9e3779b9UL^seed;
    for(j=0;j<64;++j) {
        u32 x,y,v;state=state*1664525UL+1013904223UL;
        x=state;y=(state&0xffffff00UL)|j;
        switch(kind) {
        case 0:v=positive_byte(x,y);break;
        case 1:v=positive_word(x,y);break;
        case 2:v=positive_dword(x,y);break;
        case 3:v=positive_signed(x,y);break;
        case 4:v=positive_subtract(x,y);break;
        case 5:v=positive_volatile_word(x,y);break;
        case 6:v=positive_volatile_dword(x,y);break;
        case 7:v=positive_sampled_index(x,y);break;
        case 8:v=positive_member(x,y);break;
        case 9:v=positive_loop(x,y);break;
        case 10:v=shift_masked(x,y);break;
        default:v=shift_byte(x,y);break;
        }
        result=(result<<1)^(result>>31)^v;
    }
    return result;
}
