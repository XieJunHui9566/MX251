#include "symbolic-index.h"

RAM u8 si_bytes[256];
RAM u16 si_words[128];
RAM u32 si_longs[64];
volatile RAM u8 si_volatile[64];

u16 si_touch(u16 index,u8 seed) ABI
{
    u8 slot=(u8)(index+seed);
    si_bytes[slot]=(u8)(si_bytes[slot]+seed+13);
    si_words[index&127]^=(u16)(seed+257U);
    si_longs[index&63]+=0x10203UL+seed;
    si_volatile[index&63]=(u8)(seed^index);
    return (u16)(((u16)si_bytes[slot]<<8)^si_words[index&127]);
}

void si_init(u8 seed) ABI
{
    u16 i;
    for(i=0;i<256;++i)si_bytes[i]=(u8)(i*29U+seed);
    for(i=0;i<128;++i)si_words[i]=(u16)(i*251U+seed);
    for(i=0;i<64;++i) {
        si_longs[i]=0x1020408UL*i+seed;
        si_volatile[i]=(u8)(i^seed);
    }
}

u32 stress_run(u8 kind,u8 seed)
{
    si_init(seed);
    switch(kind) {
    case 0:return si_byte(seed);
    case 1:return si_word(seed);
    case 2:return si_long(seed);
    case 3:return si_offset(seed);
    case 4:return si_branch(seed);
    case 5:return si_negative(seed);
    case 6:return si_dynamic(si_bytes+(seed&63),seed);
    default:return si_ordered(seed);
    }
}
