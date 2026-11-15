#include "call-transport.h"
static u16 __xdata words[8];
static u8 __xdata bytes[8];
/* Same spelling, different signature and local symbol in another C unit. */
static u16 take(const u16 *p,u8 index) { return p[index]+0x1357U; }
static u8 escaped(const u8 *p,u8 index) { return p[index]^0xa7; }
static u8 (* volatile hook)(const u8 *,u8)=escaped;

void transport_put(u8 *p,u8 index) { p[index]^=index*11U+7; }
u32 transport_right(u8 seed)
{
    u8 i;
    u32 sum=seed^0x12345678UL;
    for(i=0;i<8;++i) {
        words[i]=0x8100U+seed*67U+i*293U;
        bytes[i]=seed+i;
    }
#define STEP(k) sum=(sum<<5)^take(words,(seed+(k))&7); sum+=hook(bytes,k)
    STEP(0); STEP(1); STEP(2); STEP(3);
    STEP(4); STEP(5); STEP(6); STEP(7);
#undef STEP
    return sum;
}
