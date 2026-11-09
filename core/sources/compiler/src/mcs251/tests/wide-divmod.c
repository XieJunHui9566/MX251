typedef unsigned long u32;
typedef signed long s32;
#ifndef ABI
#define ABI __reentrant __sdcccall(4)
#endif

u32 pair_unsigned(u32 a,u32 b) ABI {u32 q=a/b;return (q<<3)^(a%b);}
u32 pair_signed(s32 a,s32 b) ABI {s32 q=a/b;return ((u32)q<<3)^(u32)(a%b);}
u32 pair_twice(u32 a,u32 b) ABI {u32 q=a/b;return q+(a%b)+(a%b);}
u32 different_divisor(u32 a,u32 b) ABI {u32 q=a/b;return q+(a%(b+1));}
u32 different_dividend(u32 a,u32 b) ABI {u32 q=a/b;return q+((a+1)%b);}
u32 branch_division(u32 a,u32 b) ABI {u32 q=1;if(a&1)q=a/b;return q+(a%b);}
u32 volatile_divisor(u32 a,u32 b) ABI {volatile u32 divisor=b;u32 q=a/divisor;divisor=b+1;return q+(a%divisor);}
u32 separate_blocks(u32 a,u32 b) ABI {u32 q=a/b;if(q&1)return q;return q+(a%b);}
u32 quotient_only(u32 a,u32 b) ABI {return a/b;}
u32 remainder_only(u32 a,u32 b) ABI {return a%b;}

/* Board inputs cover byte divisors, wider divisors and signed extrema. */
u32 unsigned_pipeline(unsigned char seed) ABI {
    u32 a=0xffffffffUL-(u32)seed*0x1020304UL;
    u32 b=(u32)seed*4+1;
    return pair_unsigned(a,b)^pair_twice(a,b)^different_divisor(a,b)^
        different_dividend(a,b)^branch_division(a,b)^volatile_divisor(a,b)^
        separate_blocks(a,b)^quotient_only(a,b)^remainder_only(a,b);
}
u32 signed_calibration(unsigned char seed) ABI {
    s32 a=(-2147483647L-1L)+(s32)seed*1234567L;
    s32 b=(s32)seed*1031L+2;
    return pair_signed(a,b)^pair_signed(a,-b)^pair_signed(-(a+1),b)^pair_signed(-(a+1),-b);
}
