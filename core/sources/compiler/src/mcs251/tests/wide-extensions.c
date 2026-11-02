typedef unsigned char u8;typedef unsigned int u16;typedef signed char s8;typedef signed long s32;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 right8_add(u8 a,u32 b) ABI{return b + (u32)a;}
u32 left8_add(u8 a,u32 b) ABI{return (u32)a + b;}
u32 right8_sub(u8 a,u32 b) ABI{return b - (u32)a;}
u32 left8_sub(u8 a,u32 b) ABI{return (u32)a - b;}
u32 right8_xor(u8 a,u32 b) ABI{return b ^ (u32)a;}
u32 left8_xor(u8 a,u32 b) ABI{return (u32)a ^ b;}
u32 right8_or(u8 a,u32 b) ABI{return b | (u32)a;}
u32 left8_or(u8 a,u32 b) ABI{return (u32)a | b;}
u32 right8_and(u8 a,u32 b) ABI{return b & (u32)a;}
u32 left8_and(u8 a,u32 b) ABI{return (u32)a & b;}
u32 right16_add(u16 a,u32 b) ABI{return b + (u32)a;}
u32 left16_add(u16 a,u32 b) ABI{return (u32)a + b;}
u32 right16_sub(u16 a,u32 b) ABI{return b - (u32)a;}
u32 left16_sub(u16 a,u32 b) ABI{return (u32)a - b;}
u32 right16_xor(u16 a,u32 b) ABI{return b ^ (u32)a;}
u32 left16_xor(u16 a,u32 b) ABI{return (u32)a ^ b;}
u32 right16_or(u16 a,u32 b) ABI{return b | (u32)a;}
u32 left16_or(u16 a,u32 b) ABI{return (u32)a | b;}
u32 right16_and(u16 a,u32 b) ABI{return b & (u32)a;}
u32 left16_and(u16 a,u32 b) ABI{return (u32)a & b;}
u32 signed_add(u32 a,u32 b) ABI{return b + (u32)(s32)(s8)a;}
u32 signed_sub(u32 a,u32 b) ABI{return b - (u32)(s32)(s8)a;}
u32 signed_xor(u32 a,u32 b) ABI{return b ^ (u32)(s32)(s8)a;}
u32 signed_or(u32 a,u32 b) ABI{return b | (u32)(s32)(s8)a;}
u32 signed_and(u32 a,u32 b) ABI{return b & (u32)(s32)(s8)a;}
u32 multiuse(u32 a,u32 b) ABI{u32 c=(u8)a;return (c+b)+(c^b);}
u32 identity(u32 a,u32 b) ABI{return a^b;}
u32 call_barrier(u32 a,u32 b) ABI{u32 c=(u8)a;u32 v=identity(b,a);return c+v;}
u32 branch_barrier(u32 a,u32 b) ABI{u32 c=(u8)a;if(b&1UL)return c+b;return c-b;}
