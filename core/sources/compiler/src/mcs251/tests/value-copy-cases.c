#include "value-copy.h"
static u8 __xdata octets[64];
static volatile u16 __xdata sample;
void copy_init(void)
{
    u8 i;
    for(i=0;i<64;++i)octets[i]=(i*29)^0xa7;
}
u32 copy_case(u8 seed)
{
    u32 result=copy_fanout(seed,0xffe0U+seed,0x9876U^seed,seed^0x5a,
                         0x81020304UL+seed,octets+(seed&15));
    result=(result<<5)^result^copy_mutating(0xffe0U+seed,seed);
    sample=0xff80U+seed;
    result=(result<<5)^result^copy_snapshot(&sample,seed);
    result=(result<<5)^result^copy_pointer(octets+(seed&15),seed);
    return result;
}
