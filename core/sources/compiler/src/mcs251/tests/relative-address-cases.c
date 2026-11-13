#include "relative-address.h"
static u8 __xdata bytes[24];
u32 address_tag;
u8 address_read(const u8 *p) {return *p;}

#define MIX(value) result=(result<<3)^result^(value)
u32 address_case(u8 kind,u8 seed)
{
    u8 i,offset=seed&7;
    u32 result=0x13579bdfUL;
    for(i=0;i<24;++i)bytes[i]=(seed*73U+i*29U+0x81)^(seed>>1);
    address_tag=0x81020300UL+(u32)seed*0x01030507UL;
    if(!kind) {
        MIX(address_forward(bytes));MIX(address_reverse(bytes));
        MIX(address_negative(bytes+8));MIX(address_volatile(bytes));
        MIX(address_changed(bytes,offset));
    } else {
        MIX(address_calls(bytes));MIX(address_branch(bytes,seed&1));
        MIX(address_scratch(bytes,address_tag));
        address_store(bytes);
        MIX(address_forward(bytes));MIX(address_reverse(bytes));
        MIX((u16)bytes[3]*256+bytes[8]);
    }
    return result;
}
