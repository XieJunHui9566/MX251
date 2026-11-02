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
u32 call3(u32 a,u32 b) ABI {return h3(a,b,a^b)+a;}
u32 call4(u32 a,u32 b) ABI {return h4(a,b,a^b,~a)+b;}
u32 call8(u32 a,u32 b) ABI {return h8(a,b,(u8)a,(u16)b,a^b,(u8)(a>>8),(u16)(b>>16),~a)+a+b;}
u32 swapped(u32 a,u32 b) ABI {return h4(b,a,a,b)+a+b;}
u32 pressure(u32 a,u32 b) ABI {u32 v0=a+1,v1=a+2,v2=a+3,v3=a+4,v4=a+5,v5=a+6,v6=a+7,v7=a+8;u32 c=h8(v0,v7,(u8)v1,(u16)v2,v3,(u8)v4,(u16)v5,v6);return (c+v0+v1+v2+v3+v4+v5+v6+v7)^b;}
u32 recursive(u32 a,u32 b) ABI {return recur4(a,b&7UL,a^b,b)+a+b;}
u32 legacy_target(u32 a,u32 b) ABI {return oldh4(a,b,a^b,~a)+a;}
u32 local_object(u32 a,u32 b) ABI {u32 v[2];v[0]=a;v[1]=b;return hptr(v,b,a^b,~b)+v[0]+v[1];}
u32 signed_stack(u32 a,u32 b) ABI {return (u16)hsigned(a,b,(s8)a,(s16)b);}
u32 high24(u32 a,u32 b) ABI {return (a>>24)+b;}
u32 high25(u32 a,u32 b) ABI {return (a>>25)+b;}
u32 high26(u32 a,u32 b) ABI {return (a>>26)+b;}
u32 high27(u32 a,u32 b) ABI {return (a>>27)+b;}
u32 high28(u32 a,u32 b) ABI {return (a>>28)+b;}
u32 high29(u32 a,u32 b) ABI {return (a>>29)+b;}
u32 high30(u32 a,u32 b) ABI {return (a>>30)+b;}
u32 high31(u32 a,u32 b) ABI {return (a>>31)+b;}
u32 high_pressure(u32 a,u32 b) ABI {u32 v0=a+0x01234567UL,v1=a+0x02468aceUL,v2=a+0x0369d035UL,v3=a+0x048d159cUL,v4=a+0x05b05b03UL,v5=a+0x06d3a06aUL,v6=a+0x07f6e5d1UL,v7=a+0x091a2b38UL,v8=a+0x0a3d709fUL;u32 c=h4(a,b,v0,v8);return c+(v0>>24)+(v1>>25)+(v2>>26)+(v3>>27)+(v4>>28)+(v5>>29)+(v6>>30)+(v7>>31)+(v8>>24);}
