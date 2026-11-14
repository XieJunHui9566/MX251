/* Ordinary data-pointer casts must preserve a proven object's identity.
 * No device header, register name or SDK dependency. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __sdcccall(TEST_ABI)

volatile u8 __edata cells[8];
volatile u8 __edata other[8];
const u8 __code code_cells[8]={1,2,3,4,5,6,7,8};
volatile u8 *volatile cursor;

u32 positive_generic(u8 x) ABI
{
    volatile u8 *p=cells;
    u8 i=0;u32 result=0;
    do {
        p[1]=x+i;
        result=(result<<2)^p[1];
        result+=p[3];
    } while(++i!=7);
    return result;
}

u32 positive_roundtrip(u8 x) ABI
{
    volatile u8 *p=cells;
    volatile u8 __xdata *q=(volatile u8 __xdata *)(p+2);
    volatile u8 *alias=q;
    u8 i=0;u32 result=0;
    do {
        alias[1]=x+i;
        result=(result<<2)^q[1];
        result+=alias[-1];
    } while(++i!=7);
    return result;
}

u8 positive_stack(u8 x) ABI
{
    volatile u8 local[4];
    volatile u8 __xdata *p=(volatile u8 __xdata *)local;
    volatile u8 *alias=p;
    alias[1]=x;
    alias[2]=x^0x5a;
    return alias[1]+alias[2];
}

u8 negative_parameter(volatile u8 *p,u8 x) ABI
{
    p[1]=x;
    return p[2];
}

u8 negative_merge(u8 x) ABI
{
    volatile u8 *p;
    if(x&1)p=cells;
    else p=other;
    p[1]=x;
    return p[2];
}

u8 negative_sampled_pointer(u8 x) ABI
{
    cursor=cells;
    cursor[1]=x;
    return cursor[1];
}

u8 negative_code(u8 x) ABI
{
    const u8 *p=(const u8 *)code_cells;
    return p[x&7];
}

u8 negative_integer(u8 x) ABI
{
    u16 address=(u16)(u32)&cells;
    volatile u8 *p=(volatile u8 *)(u32)address;
    p[1]=x;
    return p[2];
}
