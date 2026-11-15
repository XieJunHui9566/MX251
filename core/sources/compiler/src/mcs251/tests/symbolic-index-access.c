#include "symbolic-index.h"

u32 si_byte(u8 seed) ABI
{
    u16 i; u32 result=seed;
    for(i=0;i<32;++i) {
        u8 index=(u8)(i+seed);
        result+=si_bytes[index];
        result^=si_touch(index,seed);
        si_bytes[index]^=(u8)result;
        result+=si_touch(i,seed);
        result+=si_bytes[index];
    }
    return result;
}

u32 si_word(u8 seed) ABI
{
    u16 i; u32 result=seed;
    for(i=0;i<32;++i) {
        u16 index=(u16)(i+seed);
        result+=si_words[index];
        result^=si_touch(index,seed);
        result+=si_touch(i,seed);
        result^=si_touch((u16)result,seed);
    }
    return result;
}

u32 si_long(u8 seed) ABI
{
    u16 i; u32 result=seed;
    for(i=0;i<32;++i) {
        u16 index=(u16)(i+(seed&31));
        result+=si_longs[index];
        result^=si_touch(index,seed);
        result+=si_touch(i,seed);
        result^=si_touch((u16)result,seed);
    }
    return result;
}

u32 si_offset(u8 seed) ABI
{
    u16 i; u32 result=seed;
    u8 *const p=si_bytes+7;
    for(i=0;i<32;++i) {
        result+=p[i];
        result^=si_touch(i,seed);
        p[i]=(u8)(result+seed);
        result+=si_touch(i,seed);
        result^=p[i];
        result+=si_touch((u16)result,seed);
    }
    return result;
}

u32 si_branch(u8 seed) ABI
{
    u16 i; u32 result=seed;
    for(i=0;i<32;++i) {
        u8 *p=si_bytes+3;
        switch((i+seed)&3) {
        case 0:p=si_bytes+65;break;
        case 1:p=si_bytes+129;break;
        default:break;
        }
        result+=p[i];
        result^=si_touch(i,seed);
        p[i]^=(u8)result;
        result+=p[i];
    }
    return result;
}

u32 si_negative(u8 seed) ABI
{
    s16 i; u32 result=seed;
    u8 *p=si_bytes+32;
    for(i=-16;i<16;++i) {
        result+=p[i];
        result^=si_touch((u16)(i+16),seed);
        p[i]^=(u8)result;
        result+=p[i];
    }
    return result;
}

u32 si_dynamic(u8 *p,u8 seed) ABI
{
    u16 i; u32 result=seed;
    for(i=0;i<32;++i) {
        result+=p[i];
        result^=si_touch(i,seed);
        p[i]^=(u8)result;
        result+=p[i];
    }
    return result;
}

u32 si_ordered(u8 seed) ABI
{
    u16 i; u32 result=seed;
    for(i=0;i<32;++i) {
        result+=si_volatile[i];
        result^=si_touch(i,seed);
        si_volatile[i]^=(u8)result;
        result+=si_volatile[i];
    }
    return result;
}
