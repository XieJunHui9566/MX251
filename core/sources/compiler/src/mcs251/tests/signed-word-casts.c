typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 q0(u32 x) ABI {return (u32)(signed long)(signed int)(x>>0);}
u32 z0(u32 x) ABI {return (u32)(unsigned int)(x>>0);}
u32 q8(u32 x) ABI {return (u32)(signed long)(signed int)(x>>8);}
u32 z8(u32 x) ABI {return (u32)(unsigned int)(x>>8);}
u32 q16(u32 x) ABI {return (u32)(signed long)(signed int)(x>>16);}
u32 z16(u32 x) ABI {return (u32)(unsigned int)(x>>16);}
