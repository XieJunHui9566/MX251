/* 局部对象按真实内存访问；外部调用、易失字段和动态地址必须保持语义。 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef union { u32 u; float f; u16 w[2]; u8 b[4]; } U;
void modify_bytes(u8 *) __reentrant;
void fill_pad(u8 *) __reentrant;

u32 union_copy(u32 x, u32 y) __reentrant {
    U a,b; a.u=x; b.f=a.f; return b.u^y;
}
u32 union_partial(u32 x, u32 y) __reentrant {
    U a; a.u=x; a.b[1]=(u8)y; a.w[1]^=(u16)(y>>8); return a.u;
}
u32 union_alias_call(u32 x, u32 y) __reentrant {
    U a; a.u=x; modify_bytes(a.b); return a.u^y;
}
u32 union_volatile(u32 x, u32 y) __reentrant {
    volatile U a; a.u=x; a.b[2]=(u8)y; return a.u;
}
u32 union_volatile_field(u32 x, u32 y) __reentrant {
    union { volatile u32 u; volatile float f; } a;
    a.u=x; return a.u^y;
}
u32 union_postincrement(u32 x, u32 y) __reentrant {
    U a; u16 *p; u16 h,l; a.u=x; p=a.w; h=*p++; l=*p; return ((u32)l<<16)^h^y;
}
u32 union_dynamic(u32 x, u32 y) __reentrant {
    U a,b; u32 *p; a.u=x; b.u=y; p=(x&1)?&a.u:&b.u; *p^=0x13579bdfUL; return a.u+b.u;
}
u32 union_cross_page(u32 x, u32 y) __reentrant {
    U a; u8 pad[260]; a.u=x; fill_pad(pad); return a.u^y^pad[0]^((u32)pad[259]<<16);
}
u32 union_bitfield(u32 x, u32 y) __reentrant {
    union { u8 raw; struct { unsigned char low:3; unsigned char high:5; } bits; } a;
    a.raw=(u8)x; a.bits.low=(u8)y; return a.raw;
}
u32 union_literal(u32 x, u32 y) __reentrant {
    U a; a.u=0xa55a1234UL; modify_bytes(a.b); return a.u^x^y;
}
u32 union_repeated(u32 x, u32 y) __reentrant {
    U a; a.u=x; a.u^=y; a.u+=0x98765432UL; return a.u;
}
u32 union_control(u32 x, u32 y) __reentrant {
    U a; a.u=x; if(y&1) a.u^=y; else modify_bytes(a.b);
    if(a.u==0) return 0x76543210UL; return a.u;
}
u32 union_struct(u32 x, u32 y) __reentrant {
    struct { u8 guard; U value; u8 tail; } a;
    a.guard=0x69; a.tail=0x96; a.value.u=x; a.value.b[3]=(u8)y;
    return a.value.u ^ ((u32)a.guard<<8) ^ ((u32)a.tail<<16);
}
u32 union_volatile_pointer(u32 x, u32 y) __reentrant {
    U a; volatile u32 *p=&a.u; *p=x; return *p^y;
}
