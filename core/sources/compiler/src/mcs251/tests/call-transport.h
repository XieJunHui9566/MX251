typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
u32 transport_left(u8 seed);
u32 transport_right(u8 seed);
void transport_put(u8 *p,u8 index);
u32 transport_case(u8 kind,u8 seed);
