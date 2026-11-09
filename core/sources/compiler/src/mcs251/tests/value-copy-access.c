#include "value-copy.h"

u32 copy_fanout(u8 seed,u16 a,u16 b,u8 mode,u32 bias,const u8 *p)
{
    u8 i;
    u32 result=0x2468ace0UL,part;
    for(i=0;i<16;++i) {
        u8 which=(seed+i)&3;
        if(which==0)part=copy_sink0(a,b,p[i],mode,bias);
        else if(which==1)part=copy_sink1(a,b,p[i],mode,bias);
        else if(which==2)part=copy_sink2(a,b,p[i],mode,bias);
        else part=copy_sink3(a,b,p[i],mode,bias);
        result=(result<<3)^result^part;
    }
    return result;
}

u16 copy_mutating(u16 x,u8 seed)
{
    u16 total=0;
    u8 i;
    for(i=0;i<16;++i) {
        u16 saved=x;
        x+=3;
        if((seed+i)&1)x^=0x8123;
        total^=saved+x;
        if((seed+i)&2)total+=saved;
    }
    return total;
}

u16 copy_snapshot(volatile u16 *p,u8 seed)
{
    u16 before=*p,after;
    *p=before+(u16)seed+0x101;
    after=*p;
    return before^after;
}

u16 copy_pointer(const u8 *p,u8 seed)
{
    const u8 *saved=p;
    p+=(seed&7)+1;
    return ((u16)*saved<<8)|*p;
}
