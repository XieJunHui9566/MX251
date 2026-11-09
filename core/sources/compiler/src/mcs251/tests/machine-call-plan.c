#include "machine-call-plan.h"
#define CALLS barrier();barrier();barrier();barrier();barrier();barrier();
u16 repeated_word(u16 x,u16 y) NATIVE
{
    u16 a=x^y;
    CALLS
    return a+1;
}
u32 repeated_dword(u32 x,u32 y) NATIVE
{
    u32 a=x+y;
    CALLS
    return a^0x12345678UL;
}
u32 branches(u32 x,u32 y) NATIVE
{
    u32 a=x+y;
    CALLS
    if(x&1) {
        a^=y;
        CALLS
    } else {
        a+=x;
        CALLS
    }
    return a^x;
}
u16 loop_values(u16 x,u16 y) NATIVE
{
    u8 i;
    for(i=0;i<3;i++) {
        CALLS
        x+=y;
        y^=x;
    }
    return x+y;
}
u16 redefine(u16 x,u16 y) NATIVE
{
    CALLS
    x+=y;
    CALLS
    x^=y;
    CALLS
    return x;
}
