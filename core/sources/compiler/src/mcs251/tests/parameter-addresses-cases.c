#include "parameter-addresses.h"
static u8 PTR octets[2];
void pa_touch(u16 *p,u8 seed) {*p+=0x1234+seed;}
u32 pa_case(u8 seed)
{
    u32 tag=0x81020300UL+seed,x=0xabcdef80UL+seed,result;
    octets[0]=seed+3;octets[1]=seed^0xa5;
    result=pa_ordinary(tag,0xffe0U+seed,0x8001U^seed,4);
    result^=pa_native(tag,0xa5a5f00dUL,seed,0xff80U+seed,x,octets,octets+1);
    result^=pa_alias(tag,0xff80U+seed,seed);
    result^=pa_partial(tag,x,seed);
    result^=pa_volatile(tag,0xffe0U+seed,seed);
    return result;
}
