#include "address-remat.h"
static u8 __xdata bytes[32];
static remat_record __xdata record;

u8 remat_touch(u8 *p,u8 seed)
{
    *p^=seed*13U+7;
    return *p;
}

u32 remat_case(u8 kind,u8 seed)
{
    u8 i,count=3+(seed&3);
    u32 result;
    for(i=0;i<32;++i)bytes[i]=seed*73U+i*29U+0x81;
    record.a=seed+3;record.b=seed*7U;
    record.c=seed^0x81;record.d=seed+17;
    record.x=0xf100U+seed*137U;record.y=0x1234U+seed*61U;
    record.tag=0x81020300UL+(u32)seed*0x01030507UL;
    if(!kind) {
        result=remat_fields(&record,count);
        result^=remat_cursor(bytes,count);
        result^=(u32)record.x<<16|record.y;
    } else {
        result=remat_snapshot(bytes,count);
        result^=remat_branch(bytes,seed);
        result^=(u32)bytes[5]<<24|(u32)bytes[4]<<16;
    }
    return result;
}
