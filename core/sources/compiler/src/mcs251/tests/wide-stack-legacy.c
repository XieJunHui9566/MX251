typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 h3(u32,u32,u32) ABI;
u32 h4(u32,u32,u32,u32) ABI;
u32 h8(u32,u32,u8,u16,u32,u8,u16,u32) ABI;
u32 recur4(u32,u32,u32,u32) ABI;
u32 oldh4(u32,u32,u32,u32) ABI;
u32 hptr(u32 __xdata *,u32,u32,u32) ABI;
s16 hsigned(u32,u32,s8,s16) ABI;
float pick4(u32,u32,float,float) ABI;
float relay0(float,float) ABI;float relay1(float,float) ABI;
u32 oldh4(u32 a,u32 b,u32 c,u32 d) ABI {return ((a<<3)|(a>>29))+(b^d)+c;}
u32 legacy_caller(u32 a,u32 b) ABI {return h4(b,a,a^b,~b)+a;}
u32 float_transport(u32 a,u32 b) ABI {union {u32 u;float f;} x,y,z;x.u=a;y.u=b;z.f=relay0(x.f,y.f);return z.u;}
u32 float_transport_first(u32 a,u32 b) ABI {union {u32 u;float f;} x,y,z;x.u=a;y.u=b;z.f=relay1(x.f,y.f);return z.u;}
