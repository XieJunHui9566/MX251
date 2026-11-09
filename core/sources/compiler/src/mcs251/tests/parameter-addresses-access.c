#include "parameter-addresses.h"
static void swap16(u16 *a,u16 *b) {u16 t=*a;*a=*b;*b=t;}
static void step8(u8 *p,u8 n) {*p+=n;*p^=0x81;}
static void step16(u16 *p,u8 n) {*p+=0x101+n;*p^=0xa55a;}
static void step32(u32 *p,u32 n) {*p^=n;*p+=0x87654321UL;}
static void swap_pointer(u8 PTR **p,u8 PTR **q) {u8 PTR *t=*p;*p=*q;*q=t;}

u32 pa_ordinary(u32 tag,u16 x,u16 y,u8 count)
{
    while(count) {
        if((x^y)&1)swap16(&x,&y);
        x+=count;y^=(u16)tag;--count;
    }
    return tag^((u32)x<<16)^y;
}

u32 pa_native(u32 tag,u32 guard,u8 a,u16 b,u32 c,u8 PTR *p,u8 PTR *q) ABI
{
    step8(&a,(u8)tag);step16(&b,a);step32(&c,tag);
    if(a&1)swap_pointer(&p,&q);
    return tag^guard^a^((u32)b<<16)^c^*p^((u32)*q<<8);
}

u32 pa_alias(u32 tag,u16 x,u8 seed)
{
    u16 before=x;step16(&x,seed);pa_touch(&x,seed);
    return tag^((u32)before<<16)^x;
}

u32 pa_partial(u32 tag,u32 x,u8 seed)
{
    u8 *p=(u8 *)&x;p[1]^=seed;p[3]+=7;
    return tag^x;
}

u32 pa_volatile(u32 tag,u16 x,u8 seed)
{
    volatile u16 *p=&x;*p+=seed;*p^=0x1357;
    return tag^x;
}
