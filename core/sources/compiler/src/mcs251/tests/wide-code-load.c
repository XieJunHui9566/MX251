typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef union {float f;u32 u;} bits;
#define ABI __reentrant __sdcccall(4)
extern const __code u8 code_table[520];
extern const __code float code_floats[8];
extern const __code u16 code_words[16];
extern const __code u32 code_longs[16];

u32 c_byte(u32 n) ABI {return code_table[n&511];}
u32 c_word(u32 n) ABI {return code_words[n&15];}
u32 c_long(u32 n) ABI {return code_longs[n&15];}
float c_float(u32 n) ABI {return code_floats[n&7];}
u32 c_unaligned(u32 n) ABI {return *(const u32 __code *)(code_table+(n&511));}
u32 c_signed(u32 n) ABI {return (long)*(const signed char __code *)(code_table+(n&511));}
u32 c_walk(u32 n) ABI {
    const u8 __code *p=code_table+(n&255);u8 i;u32 sum=0;
    for(i=0;i<12;++i){sum+=*p;++p;}return sum;
}
u32 c_branch(u32 n) ABI {
    const u8 __code *p;
    if(n&1)p=code_table+(n&255);else p=code_table+256+(n&255);
    return (u32)p[0]+((u32)p[3]<<16);
}
u32 c_volatile(u32 n) ABI {
    const volatile u32 __code *p=(const volatile u32 __code *)(code_table+(n&511));
    u32 a=*p;u32 b=*p;return a^b;
}
u32 c_pair(u32 a,u32 b) ABI {return c_unaligned(a)^c_long(b);}
u32 c_literal(u32 a) ABI {return *(const u16 __code *)(0xff80f0UL+(a&511));}
u32 c_recurse(u32 a,u32 n) ABI {
    u8 v=code_table[a&511];
    if(n)return v+c_recurse(a+1,n-1);
    return v;
}
