typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef union {u32 u;float f;u8 b[4];} bits;
#define ABI __reentrant __sdcccall(4)
/* 各全表达式分隔真实易失访问，已读取值可以重复使用，但不能合并读取。 */
u32 v_sequence(u32 address,u32 x) ABI {
    volatile u32 *p=(volatile u32 *)address;u32 a,b,c;
    p[0]=x;a=p[0];p[0]=x^0xa5a5a5a5UL;b=p[0];p[1]=a+b;c=p[1];
    return a^(b<<1)^c;
}
u32 v_twice(u32 address,u32 x) ABI {
    volatile u32 *p=(volatile u32 *)address;u32 a,b;
    *p=x;a=*p;b=*p;return a+(b>>8);
}
u32 v_short(u32 address,u32 x) ABI {
    volatile u16 *p=(volatile u16 *)address;u16 a,b;
    *p=(u16)x;a=*p;*p=a^0x5a5a;b=*p;return ((u32)a<<16)|b;
}
u32 v_change(u32 address,u32 x) ABI {
    volatile u32 *p=(volatile u32 *)address;u32 a=*p;
    *p=a^x;return *p;
}
u32 v_alias(u32 x,u32 n) ABI {
    volatile bits c;u32 a,b;c.u=x;a=c.u;b=v_change((u32)&c.u,n);
    return a^b^(c.u+1);
}
u32 v_roundtrip(u32 x,u32 n) ABI {
    volatile bits c;u32 a,b;c.u=x;a=c.u;c.b[n&3]^=0xa5;
    b=c.u;c.u=b+1;return a^(a>>8)^b^c.u;
}
u32 v_float_bits(u32 x) ABI {
    volatile bits a,b;a.u=x;b.f=a.f;return b.u;
}
u32 v_branch(u32 x,u32 n) ABI {
    volatile bits a;u32 v;a.u=x;
    if(n&1)a.b[0]^=0x81;else a.b[3]^=0x42;
    v=a.u;return (v>>7)^(v<<9);
}
u32 v_loop(u32 x,u32 n) ABI {
    volatile bits c;u32 sum=0;u8 i;c.u=x;
    for(i=0;i<8;++i){sum+=c.u;c.b[(i+n)&3]^=(u8)(x>>i);}
    return sum^c.u;
}
u32 v_signed(u32 x) ABI {
    volatile bits c;c.u=x;
    return (u32)(signed long)(signed char)c.b[0]^(u32)(signed long)(signed int)(c.u>>8);
}
