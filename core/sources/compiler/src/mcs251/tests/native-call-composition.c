#include "native-call-composition.h"
__data u32 source_rate, sample_a;
__data u16 sample_word;
__data u8 sample_byte;

u16 constant_reload(u32 divisor)
{
    switch(divisor) {
    case 1UL: return 0x1357;
    case 256UL: return 0x2468;
    case 65537UL: return 0xabcd;
    case 115200UL: return 0x9876;
    default: return (u16)(0u-(u16)((30000000UL/divisor+2)/4));
    }
}

u16 object_reload(u32 divisor)
{
    return (u16)(0u-(u16)((source_rate/divisor+2)/4));
}

u32 snapshot_calls(u32 keep)
{
    u32 first=native_mix(sample_a,keep,sample_word);
    return native_mix(sample_a,first,sample_word)^keep;
}

u8 byte_snapshot(u8 value)
{
    return native_byte(sample_byte,value);
}

u32 native_work(u8 seed)
{
    static const __code u32 divisors[16]={
        1UL,2UL,3UL,7UL,255UL,256UL,257UL,65535UL,
        65536UL,65537UL,0x7fffffffUL,0x80000000UL,0xffffffffUL,
        300UL,115200UL,460800UL
    };
    u32 divisor=divisors[seed&15], result;
    source_rate=0xffffffffUL-0x01010101UL*seed;
    sample_a=0x01234567UL+seed;
    sample_word=(u16)(0xff00u+(u16)seed*17);
    sample_byte=seed^0xa5;
    result=snapshot_calls(source_rate^divisor);
    result^=((u32)object_reload(divisor)<<16)|constant_reload(divisor);
    result^=byte_snapshot(seed);
    return result^sample_a^sample_word;
}
