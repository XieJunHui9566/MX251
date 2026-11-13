#include "byte-pack.h"
static u8 __xdata data[4];

#define MIX(value) result=(result<<3)^result^(value)
u32 pack_case(u8 kind,u8 seed)
{
    u32 result=0x13579bdfUL;
    u32 a=0x81020300UL+seed,b=0x7ffe4500UL^((u32)seed<<9)^seed;
    data[0]=seed*73U+0x80;data[1]=seed^0xff;
    data[2]=seed*13U+0x55;data[3]=(seed<<2)^(seed>>1)^0x81;
    if(!kind) {
        MIX(pack_be(data));MIX(pack_le(data));MIX(pack_word(data));
        MIX(pack_or(data));MIX(pack_xor(data));MIX(pack_volatile(data));
        MIX(pack_repeat(data[0],data[1]));MIX(pack_gaps(data[2],data[3]));
    } else {
        MIX(pack_signed((const s8 *)data));MIX(pack_overlap(data));
        MIX(pack_shared(data));MIX(pack_truncated(data));
        MIX(pack_pressure(a,b,data));
        MIX(pack_native(a,data[0],data[1],data[2],data[3]));
    }
    return result;
}
