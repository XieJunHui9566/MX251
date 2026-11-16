#include "memory-values.h"
__edata mv_u16 mv_a, mv_b;
__edata long mv_signed_state;
volatile __edata mv_u16 mv_observed;
__edata __at(0x300) mv_u16 mv_fixed;
volatile __xdata __at(0x12000) mv_u8 mv_io[4];

mv_u32 mv_constant(void) MV_ABI {
    mv_a=0x1234; mv_b=0xfedc;
    mv_io[0]=mv_a>>8; mv_io[1]=(mv_u8)mv_a;
    mv_a+=0x1111;
    mv_io[2]=mv_b>>8; mv_io[3]=(mv_u8)mv_b;
    mv_b^=0x1010;
    return ((mv_u32)mv_a<<16)|mv_b;
}
mv_u16 mv_forward(mv_u16 seed) MV_ABI {
    mv_a=0x2000+seed*257;
    mv_io[0]=mv_a>>8; mv_io[1]=(mv_u8)mv_a;
    mv_a+=seed;
    return mv_a^(((mv_u16)mv_io[0]<<8)|mv_io[1]);
}
mv_u16 mv_alias(mv_u16 seed, volatile __xdata mv_u16 *pointer) MV_ABI {
    mv_u16 before;
    mv_a=seed; before=mv_a;
    *pointer=seed^0x5aa5;
    return before^mv_a;
}
mv_u16 mv_partial(mv_u16 seed) MV_ABI {
    mv_a=seed*257+0x1234;
    ((volatile __xdata mv_u8 *)&mv_a)[1]=0x5a;
    return mv_a;
}
mv_u16 mv_call(mv_u16 seed) MV_ABI {
    mv_a=seed; mv_change(seed+0x4312);
    return mv_a;
}
mv_u16 mv_callback(mv_u16 seed, void (*change)(mv_u16) MV_ABI) MV_ABI {
    mv_a=seed; change(seed^0xa55a);
    return mv_a;
}
mv_u16 mv_volatile(mv_u16 seed) MV_ABI {
    mv_observed=seed; mv_io[0]=(mv_u8)mv_observed;
    mv_observed=seed+1;
    return mv_observed^mv_io[0];
}
mv_u32 mv_signed(signed char seed) MV_ABI {
    mv_signed_state=seed; mv_io[0]=3;
    return (mv_u32)mv_signed_state;
}
mv_u16 mv_branch(mv_u16 seed) MV_ABI {
    mv_a=seed;
    if(seed&1)mv_change(seed+19);
    else mv_a+=7;
    return mv_a;
}
mv_u16 mv_redefine(mv_u16 seed) MV_ABI {
    mv_u16 saved;
    mv_a=seed; saved=mv_a; mv_io[0]=(mv_u8)seed;
    ++seed; mv_a=seed;
    return saved+mv_a;
}
mv_u16 mv_overlap(mv_u16 seed) MV_ABI {
    mv_fixed=seed;
    *((volatile __xdata mv_u8 *)0x301)=0xab;
    return mv_fixed;
}
