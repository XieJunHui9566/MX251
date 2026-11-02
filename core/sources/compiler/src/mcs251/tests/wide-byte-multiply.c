typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u8 low8(u8 a,u8 b) ABI {return (u8)(a*b);}
u32 widened(u32 a,u32 b) ABI {return (u8)((u8)a*(u8)b);}
u32 retained8(u32 a,u32 b) ABI {u8 p=(u8)((u8)a*(u8)b);return ((u32)p+a)^b;}
u32 constant8(u32 a,u32 b) ABI {return (u8)((u8)a*7U)+(u32)(u8)b;}
u32 chain8(u32 a,u32 b) ABI {u8 p=(u8)((u8)a*(u8)b);return (u8)(p*(u8)(a>>8));}
u32 branch8(u32 a,u32 b) ABI {if(a&0x100UL)return (u8)((u8)a*(u8)b);return (u8)((u8)(a>>8)*(u8)(b>>8));}
u32 calls8(u32 a,u32 b) ABI {u32 p=widened(a,b);return p+retained8(b,a);}
u32 pressure8(u32 a,u32 b) ABI {
 u32 v0=a+1,v1=a+2,v2=a+3,v3=a+4,v4=a+5,v5=a+6,v6=a+7,v7=a+8;
 u8 p=(u8)((u8)(v0^b)*(u8)(v7+b));
 return ((u32)p+v0+v1+v2+v3+v4+v5+v6+v7)^b;
}
