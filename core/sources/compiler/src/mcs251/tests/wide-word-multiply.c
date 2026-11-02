typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 product(u32 a,u32 b) ABI{return (u32)(u16)a*(u16)b;}
u32 swapped(u32 a,u32 b) ABI{return (u32)(u16)b*(u16)a;}
u32 squared(u32 a,u32 b) ABI{return (u32)(u16)a*(u16)a;}
u32 retained(u32 a,u32 b) ABI{u32 p=(u32)(u16)a*(u16)b;return (p+a)^b;}
u32 chained(u32 a,u32 b) ABI{u32 p=(u32)(u16)a*(u16)b;return (u32)(u16)p*(u16)(p>>16);}
u32 conditional(u32 a,u32 b) ABI{if(a&1UL)return (u32)(u16)a*(u16)b;return (u32)(u16)(a>>16)*(u16)(b>>16);}
u32 bytes(u32 a,u32 b) ABI{return (u32)(u8)a*(u8)b;}
u32 byteword(u32 a,u32 b) ABI{return (u32)(u8)a*(u16)b;}
u32 extract0(u32 a,u32 b) ABI{return (u16)(a>>0);}
u32 extract8(u32 a,u32 b) ABI{return (u16)(a>>8);}
u32 extract16(u32 a,u32 b) ABI{return (u16)(a>>16);}
u32 extract_product(u32 a,u32 b) ABI{return (u32)(u16)(a>>8)*(u16)(b>>8);}
u32 pressure(u32 a,u32 b) ABI{u32 v0=(a+0x1234567UL)^b,v1=(a+0x2468aceUL)^b,v2=(a+0x369d035UL)^b,v3=(a+0x48d159cUL)^b,v4=(a+0x5b05b03UL)^b,v5=(a+0x6d3a06aUL)^b,v6=(a+0x7f6e5d1UL)^b,v7=(a+0x91a2b38UL)^b,v8=(a+0xa3d709fUL)^b,v9=(a+0xb60b606UL)^b,v10=(a+0xc83fb6dUL)^b,v11=(a+0xda740d4UL)^b;u32 p0=(u32)(u16)v0*(u16)v1,p1=(u32)(u16)v2*(u16)v3,p2=(u32)(u16)v4*(u16)v5,p3=(u32)(u16)v6*(u16)v7,p4=(u32)(u16)v8*(u16)v9,p5=(u32)(u16)v10*(u16)v11;return (p0+p1+p2+p3+p4+p5)^(v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11);}
u32 call_product(u32 a,u32 b) ABI{u32 p=product(a,b);return p+product(b^0x98765432UL,a+0x1234UL);}
