#include "parameter-values.h"
static u8 __xdata octets[32];
static const u8 __code table[32]={
    7,13,29,31,43,61,67,79,83,97,101,127,131,149,157,173,
    179,191,193,211,223,227,229,233,239,241,251,3,5,11,17,19};

void pv_init(void)
{u8 i;for(i=0;i<32;++i)octets[i]=(i*23)^0xa9;}

u16 pv_bump(u16 *p,u8 seed)
{*p+=0x101+seed;return *p^0xa55a;}

u32 pv_case(u8 seed)
{
    u32 a=0x81020300UL+seed,b=0x7ffe4500UL^((u32)seed<<9)^seed;
    u32 result=0x13579bdfUL;
#define MIX(v) result=(result<<3)^result^(v)
    MIX(c_byte(a,b));
    MIX(c_word(a,b));
    MIX(c_dword(a,b));
    MIX(c_pointer(a,b));
    MIX(c_float(a,b));
    MIX(c_eight(a,b));
    MIX(c_loop(a,b));
    MIX(c_recurse(a,b));
    MIX(c_frame(a,b));
    MIX(c_pressure(a,b));
    MIX(pv_ordinary(a,0xff80U+seed,octets+(seed&7),4));
    MIX(pv_code(a,table+(seed&7),4));
    MIX(pv_signed(a,(s8)(-100+seed),5));
    MIX(pv_alias(a,0xff80U+seed,seed));
    MIX(pv_switch(a,0xff80U+seed,5));
    return result;
}
