/* 检查参数、局部标量、截断与 volatile，调用边界阻止局部对象被消去。 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
void stack_touch(u32 *) __reentrant;
void stack_pad(u8 *) __reentrant;

u32 stack_load(u16 marker, u32 value) __reentrant {
    return (value ^ 0x13579bdfUL) + marker;
}
u32 stack_store(u16 marker, u32 value) __reentrant {
    u32 local = value;
    stack_touch(&local);
    return local + marker;
}
u16 stack_truncate(u16 marker, u32 value) __reentrant {
    return (u16)value * marker;
}
u32 stack_cross_page(u16 marker, u32 value) __reentrant {
    u8 pad[260];
    u32 local = value;
    stack_pad(pad);
    stack_touch(&local);
    return local + marker + pad[0] + pad[259];
}
u16 stack_volatile(u16 marker, volatile u16 value) __reentrant {
    value = marker;
    return value ^ (u16)0x5aa5;
}
u32 stack_volatile32(u32 marker, volatile u32 value) __reentrant {
    value = marker;
    return value ^ 0x5aa55aa5UL;
}
