typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u32 opaque(u32) __reentrant;

/* 跨字节进位、借位与循环依赖；运行时输入不能由前端常量折叠。 */
u32 plan32(u32 x,u32 y) __reentrant {
    u8 i;
    for (i=0;i<7;++i) { x=(x+y)<<1; y=(y-x)<<2; }
    return x^y;
}
u32 plan16(u32 x,u32 y) __reentrant {
    u16 a=(u16)x,b=(u16)y; u8 i;
    for (i=0;i<7;++i) { a=(u16)((a+b)<<1); b=(u16)((b-a)<<2); }
    return ((u32)a<<16)|b;
}
u32 plan_pressure(u32 x,u32 y) __reentrant {
    u32 a=opaque(x),b=opaque(y),c=opaque(x^0xa55aa55aUL);
    u32 d=opaque(y^0x01020304UL),e=opaque(x^0xdeadbeefUL),f=opaque(y^0x87654321UL);
    u32 r=((a+b)<<1)^((c-d)<<2)^((e+f)<<3);
    return r^a^b^c^d^e^f;
}
u32 plan_volatile(u32 x,u32 y) __reentrant {
    volatile u32 a=x,b=y; u8 i;
    for (i=0;i<3;++i) { a=a+b; b=b-a; }
    return a^b;
}
