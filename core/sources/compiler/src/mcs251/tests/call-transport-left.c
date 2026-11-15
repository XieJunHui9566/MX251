#include "call-transport.h"
static u8 __xdata bytes[8];
static const u8 __code table[8]={3,19,71,129,251,17,96,203};
static u8 take(const u8 *p,u8 index) { return p[index]^0x5a; }
static u8 take_code(const u8 __code *p,u8 index) { return p[index]; }

u32 transport_left(u8 seed)
{
    u8 i;
    u32 sum=seed;
    for(i=0;i<8;++i)bytes[i]=seed*29U+i*17U;
#define STEP(k) sum=(sum<<3)^take(bytes,(seed+(k))&7); transport_put(bytes,k); sum^=take_code(table,k)
    STEP(0); STEP(1); STEP(2); STEP(3);
    STEP(4); STEP(5); STEP(6); STEP(7);
#undef STEP
    for(i=0;i<8;++i)sum+=bytes[i];
    return sum;
}
