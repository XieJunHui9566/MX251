typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#ifndef TEST_ABI
#define TEST_ABI 4
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)

u32 view_byte(s8 x) ABI
{
    u8 bits=(u8)x;
    return (u32)(s32)x+(u32)bits+((u32)(bits>>3)<<16);
}
u32 view_word(s16 x) ABI
{
    u16 bits=(u16)x;
    return (u32)(s32)x+(u32)bits+((u32)(bits>>3)<<16);
}
u32 view_signed(u16 x) ABI
{
    s16 view=(s16)x;
    return (u32)(s32)view+(u32)(s32)(view>>3)+x;
}
u32 view_long(s32 x) ABI
{
    u32 bits=(u32)x;
    return (u32)(x>>3)^(bits>>3)^((u32)(x<0)<<31);
}
u32 view_mutation(s16 x) ABI
{
    u16 old=(u16)x;
    x=(s16)(x^0x1234);
    return ((u32)old<<16)|(u16)x;
}
u32 view_branch(s16 x,u8 mode) ABI
{
    u16 bits;
    switch(mode&3) {
    case 0:bits=(u16)x;break;
    case 1:bits=(u16)(x^0x1234);break;
    default:bits=(u16)(x^0x4321);break;
    }
    return (u32)bits+(u32)(s32)x;
}
u8 view_bool(u8 x) ABI { _Bool b=(_Bool)x;return (u8)(b+x); }

__xdata u16 view_array[32];
__xdata __at(0x0010) u16 view_absolute[16];
u16 view_object(u8 i) ABI { u16 v=view_array[i];view_array[i]=(u16)(v+1);return v; }
u16 view_raw(u16 *p,u8 i) ABI { u16 v=p[i];p[i]=(u16)(v+1);return v; }
u16 view_fixed(u8 i) ABI { u16 v=view_absolute[i];view_absolute[i]=(u16)(v+1);return v; }
