/* 固定偏移、跨调用别名、分支和寄存器溢出的组合回归。 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef union { u32 u; u16 w[2]; u8 b[4]; } U;
u32 opaque(u32) __reentrant;
void modify_bytes(u8 *) __reentrant;

u32 frame_fields(u32 x,u32 y) __reentrant {
    struct { u8 before; U values[3]; u8 after; } s;
    s.before=0x69; s.after=0x96;
    s.values[0].u=x+y; s.values[1].u=x-y; s.values[2].u=x^y;
    modify_bytes(s.values[1].b);
    return s.values[0].u^s.values[1].u^s.values[2].u^((u32)s.before<<8)^s.after;
}
u32 frame_pressure(u32 x,u32 y) __reentrant {
    U a;
    u32 p0=opaque(x^0x01020304UL),p1=opaque(x^0x11223344UL);
    u32 p2=opaque(x^0x55667788UL),p3=opaque(x^0xdeadbeefUL);
    u32 p4=opaque(y^0x13579bdfUL),p5=opaque(y^0x2468ace0UL);
    u32 p6=opaque(y^0xa55aa55aUL),p7=opaque(y^0xfedcba98UL);
    a.u=p0+p1+p2+p3; modify_bytes(a.b);
    a.u^=p4+p5+p6+p7;
    return a.u^p0^p1^p2^p3^p4^p5^p6^p7;
}
u32 frame_chain(u32 x,u32 y) __reentrant {
    struct { U a; U b; } s; u8 *p; u32 *q;
    s.a.u=x; s.b.u=y;
    p=(u8 *)&s; p+=4; q=(u32 *)p;
    *q^=opaque(x); modify_bytes((u8 *)q);
    return s.a.u+s.b.u;
}
u32 frame_loop(u32 x,u32 y) __reentrant {
    U a; u8 i;
    a.u=x;
    for(i=0;i<8;++i) {
        if(y&((u32)1<<i)) a.u^=0x12345678UL; else a.u+=0x01020304UL;
        a.b[2]^=i;
    }
    return a.u^y;
}
