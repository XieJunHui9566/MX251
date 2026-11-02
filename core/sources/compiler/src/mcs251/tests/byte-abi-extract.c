typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u8 part0(u32 x) ABI{return (u8)(x>>0);}
u8 part1(u32 x) ABI{return (u8)(x>>8);}
u8 part2(u32 x) ABI{return (u8)(x>>16);}
u8 part3(u32 x) ABI{return (u8)(x>>24);}
u8 wordpart(u16 x) ABI{return (u8)(x>>8);}
u8 byteid(u8 x) ABI{return x;}
u32 pack(u32 a,u32 b) ABI{u8 v0=part0(a),v1=part1(a),v2=part2(a),v3=part3(a);return ((u32)v3<<24)|((u32)v2<<16)|((u32)v1<<8)|v0;}
u32 narrow(u32 a,u32 b) ABI{u8 v0=wordpart((u16)a),v1=byteid((u8)b);return ((u32)v0<<8)|v1;}
