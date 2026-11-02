typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
extern volatile __bit b0,b1,b2,b3;
u32 old_bit_mutate(u32,u32) ABI;
u32 bt_assign(u32 x,u32 n) ABI {b0=x;b1=n;return (u32)b0|((u32)b1<<8);}
u32 bt_cond(u32 x,u32 n) ABI {b0=x;if(b0)return n^0xa5a55a5aUL;return n+0x12345678UL;}
u32 bt_copy(u32 x,u32 n) ABI {b0=x;b1=b0;b2=n;b3=b2;return (u32)b1|((u32)b3<<8);}
u32 bt_not(u32 x,u32 n) ABI {b0=x;b1=n;return (u32)!b0|((u32)!b1<<16);}
u32 bt_sequence(u32 x,u32 n) ABI {u32 a,b;b0=x;a=b0;b0=n;b=b0;b1=0;b1=1;return a|(b<<8)|((u32)b1<<16);}
u32 bt_call(u32 x,u32 n) ABI {u32 a;b0=x;b1=n;a=old_bit_mutate(x,n);return a+(u32)b0+((u32)b1<<1);}
u32 bt_mask(u32 x,u32 n) ABI {b0=x&0x100UL;b1=n&0x80000000UL;return (u32)b0|((u32)b1<<8);}
u32 bt_loop(u32 x,u32 n) ABI {u8 i;u32 v=x;for(i=0;i<8;++i){b0=n&(1UL<<i);if(b0)v^=0x10203040UL+i;else v+=0x87654321UL;}return v;}
