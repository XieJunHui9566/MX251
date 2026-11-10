#include "native-call-composition.h"

u32 native_mix(u32 a,u32 b,u16 c) __sdcccall(4)
{
    sample_a+=0x01020304UL;
    sample_word^=(u16)b;
    return (a^b)+((u32)c<<16)+3;
}

u8 native_byte(u8 a,u8 b) __sdcccall(4)
{
    ++sample_byte;
    return (u8)(a+b+7);
}
