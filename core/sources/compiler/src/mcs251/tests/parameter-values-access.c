#include "parameter-values.h"

u32 pv_ordinary(u32 tag,u16 x,const u8 *p,u8 count)
{
    while(count) {
        x+=*p;
        if(x&1) {++p;tag^=x;} else {p+=2;tag+=x;}
        --count;
    }
    return tag^((u32)x<<16)^*p;
}

u32 pv_code(u32 tag,const u8 __code *p,u8 count)
{
    while(count) {tag=(tag<<1)^*p;++p;--count;}
    return tag^*p;
}

u32 pv_signed(u32 tag,s8 x,u8 count)
{
    while(count) {tag^=(long)x;x+=3;--count;}
    return tag^(long)x;
}

u32 pv_alias(u32 tag,u16 x,u8 seed)
{
    u16 before=x;
    tag^=pv_bump(&x,seed);
    return tag^((u32)before<<16)^x;
}

u32 pv_switch(u32 tag,u16 x,u8 count)
{
    while(count) {
        switch(x&7) {
        case 0:x+=17;break;
        case 1:x^=0x138;break;
        case 2:x-=9;break;
        case 3:x+=0x8001;break;
        case 4:x^=0x7ffe;break;
        case 5:x+=3;break;
        case 6:x-=0x101;break;
        default:x^=0xa5a5;break;
        }
        tag=(tag<<1)^x;--count;
    }
    return tag^x;
}
