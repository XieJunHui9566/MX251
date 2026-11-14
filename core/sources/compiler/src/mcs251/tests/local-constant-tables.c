/* Ordinary local tables, independent of a device or SDK. */
#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#define P24(x) ((u8 *)(x))
#define PVAL(x) ((u32)(x))
#else
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32;
#define ABI
#define __code
#define P24(x) ((u8 *)(uintptr_t)((x)&0x00ffffffUL))
#define PVAL(x) ((u32)(uintptr_t)(x))
#endif

static u8 data_a=19,data_b=231;
static const u8 __code code_a[2]={71,33},code_b[2]={43,255};
static volatile u8 sampled_index;
typedef u32 (*Operation)(u32,u8) ABI;
u32 table_add(u32 a,u8 b) ABI {return (a+0x13579UL)^b;}
u32 table_xor(u32 a,u8 b) ABI {return (a^0xabcdef01UL)+b;}

u32 bytes(u8 seed) ABI {
    u8 t[2],i;u32 r=seed;t[0]=37;t[1]=241;
    for(i=0;i<8;++i)r=(r<<3)^t[(seed+i)&1];
    return r;
}
u32 words(u8 seed) ABI {
    s16 t[4],v;u8 i;u32 r=seed;t[0]=-32768;t[1]=-1;t[2]=19;t[3]=32767;
    for(i=0;i<8;++i) {v=t[(seed+i)&3];r=(r<<3)^(u16)v;}
    return r;
}
u32 dwords(u8 seed) ABI {
    u32 t[2],r=seed;u8 i;t[0]=0xabcdef01UL;t[1]=0x76543210UL;
    for(i=0;i<8;++i)r=(r<<1)^t[(seed+i)&1];
    return r;
}
u32 data_pointers(u8 seed) ABI {
    u8 *t[2],i;u32 r=seed;t[0]=&data_a;t[1]=&data_b;
    for(i=0;i<8;++i)r=(r<<2)^*t[(seed+i)&1];
    return r;
}
u32 code_pointers(u8 seed) ABI {
    const u8 __code *t[2];u8 i;u32 r=seed;t[0]=code_a;t[1]=code_b;
    for(i=0;i<8;++i)r=(r<<2)^t[(seed+i)&1][i&1];
    return r;
}
u32 callbacks(u8 seed) ABI {
    Operation t[2],*alias;u8 i;u32 r=seed;t[0]=table_add;t[1]=table_xor;alias=t;
    for(i=0;i<8;++i)r=(*(alias+((seed+i)&1)))(r,i);
    return r;
}
u32 volatile_index(u8 seed) ABI {
    u16 t[2];u8 i;u32 r=seed;t[0]=0x1357;t[1]=0xface;
    for(i=0;i<8;++i) {sampled_index=seed+i;r=(r<<1)^t[sampled_index&1];}
    return r;
}
u32 volatile_table(u8 seed) ABI {
    volatile u16 t[2];u8 i;u32 r=seed;t[0]=0x1357;t[1]=0xface;
    for(i=0;i<8;++i)r=(r<<1)^t[(seed+i)&1];
    return r;
}
u32 mutable_table(u8 seed) ABI {
    u16 t[2];u8 i;u32 r=seed;t[0]=17;t[1]=251;
    for(i=0;i<8;++i) {t[i&1]+=(u16)seed;r=(r<<2)^t[(seed+i)&1];}
    return r;
}
void table_mutate(u16 *p,u8 seed) ABI {p[seed&1]^=(u16)seed+0x1234;}
u32 escaped_table(u8 seed) ABI {
    u16 t[2];t[0]=17;t[1]=251;table_mutate(t,seed);return t[seed&1];
}
u32 partial_table(u8 seed) ABI {
    u16 t[2];u8 *p,k;t[0]=17;t[1]=251;p=(u8 *)t;k=(seed&1)*2;p[k]=seed;p[k+1]=seed;
    /* The byte order differs on the host; fold all bytes commutatively. */
    return (u16)(p[0]+p[1]+p[2]+p[3]);
}
u32 conditional_table(u8 seed) ABI {
    u16 t[2];t[0]=17;
    if(seed&1) {t[1]=251;return t[seed&1];}
    return t[0];
}
u32 unbounded_table(u8 index) ABI {
    u16 t[2];t[0]=0x1357;t[1]=0xface;return t[index];
}
u32 wrapped_table(u8 index) ABI {
    u16 t[2];u8 *p;t[0]=0x1357;t[1]=0xface;p=(u8 *)t;
    return *(u16 *)(p+(u8)(index*2));
}
u32 eight_words(u8 seed) ABI {
    u32 t[8];t[0]=0x12345678UL;t[1]=0xfedcba98UL;t[2]=0x11223344UL;t[3]=0x88776655UL;
    t[4]=0x24681357UL;t[5]=0xaaaa5555UL;t[6]=0x3333ccccUL;t[7]=0x56789012UL;
    return t[seed&7]^seed;
}
u32 eight_callbacks(u8 seed) ABI {
    Operation t[8];t[0]=table_add;t[1]=table_xor;t[2]=table_add;t[3]=table_add;
    t[4]=table_xor;t[5]=table_add;t[6]=table_xor;t[7]=table_xor;
    return t[seed&7](0x98765432UL,seed);
}
u32 pointer_literals(u8 seed) ABI {
    u8 *t[2];u32 bits[2];t[0]=P24(0xab001234UL);t[1]=P24(0xfeed4321UL);
    bits[0]=PVAL(P24(0xde765432UL));bits[1]=PVAL(P24(0xefba9876UL));
    return PVAL(t[seed&1])+bits[seed&1];
}
u32 mixed_objects(u8 seed) ABI {
    u8 t[2];volatile u8 other[2];t[0]=73;t[1]=219;
    other[0]=seed;other[1]=seed^0xa5;
    return (u16)t[seed&1]+other[(seed>>1)&1];
}

u32 stress_run(u8 kind,u8 seed) {
    u8 i,s;u32 r=0,value;
    for(i=0;i<4;++i) {
        s=(u8)((u16)seed*4+i);
        switch(kind) {
        case 0:value=bytes(s);break;
        case 1:value=words(s);break;
        case 2:value=dwords(s);break;
        case 3:value=data_pointers(s);break;
        case 4:value=code_pointers(s);break;
        case 5:value=callbacks(s);break;
        case 6:value=volatile_index(s);break;
        case 7:value=volatile_table(s);break;
        case 8:value=mutable_table(s);break;
        case 9:value=escaped_table(s);break;
        case 10:value=partial_table(s);break;
        case 11:value=conditional_table(s);break;
        case 12:value=unbounded_table(s&1);break;
        case 13:value=wrapped_table((u8)(128+(s&1)));break;
        case 14:value=eight_words(s);break;
        case 15:value=eight_callbacks(s);break;
        case 16:value=pointer_literals(s);break;
        default:value=mixed_objects(s);break;
        }
        r=(r<<3)^value;
    }
    return r;
}
