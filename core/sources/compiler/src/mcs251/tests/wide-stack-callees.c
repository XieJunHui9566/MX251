typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 h3(u32 a,u32 b,u32 c) ABI {return (a^(b<<3))+c;}
u32 h4(u32 a,u32 b,u32 c,u32 d) ABI {return ((a<<3)|(a>>29))+(b^d)+c;}
u32 h8(u32 a,u32 b,u8 c,u16 d,u32 e,u8 f,u16 g,u32 h) ABI {return (a^b)+(u32)c+((u32)d<<8)+e+(u32)f+((u32)g<<16)+h;}
u32 recur4(u32 a,u32 n,u32 c,u32 d) ABI {if(!n)return a+c+d;return recur4(a+c,n-1,d,c)+a;}
u32 hptr(u32 __xdata *p,u32 a,u32 b,u32 c) ABI {return *p+a+(b^c);}
s16 hsigned(u32 a,u32 b,s8 c,s16 d) ABI {return (s16)((u16)c^(u16)d^(u16)(a+b));}
float pick4(u32 a,u32 b,float c,float d) ABI {if(a&1UL)return c;return d;}
float relay0(float a,float b) ABI {return pick4(0UL,1UL,a,b);}
float relay1(float a,float b) ABI {return pick4(1UL,0UL,a,b);}
