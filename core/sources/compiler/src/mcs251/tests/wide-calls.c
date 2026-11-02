typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;typedef signed long s32;
#define ABI __reentrant __sdcccall(4)
u32 h32(u32 a,u32 b) ABI;
u16 h16(u16 a,u16 b) ABI;
u8 h8(u8 a,u8 b) ABI;
u32 h816(u8 a,u16 b) ABI;
u32 h1632(u16 a,u32 b) ABI;
u32 h328(u32 a,u8 b) ABI;
s8 hs8(s8 a,s8 b) ABI;
s16 hs16(s16 a,s16 b) ABI;
u32 hzero(void) ABI;
void hvoid(void) ABI;
u32 old32(u32 a,u32 b) ABI;
u32 clobber(u32 a,u32 b) ABI;
void clobber_void(void) ABI;
u32 recur(u32 a,u32 b) ABI;
void voidtail(void) ABI;
u32 tail32(u32 a,u32 b) ABI {return h32(a,b);}
u32 swap32(u32 a,u32 b) ABI {return h32(b,a);}
u32 live32(u32 a,u32 b) ABI {return clobber(a,b)+a+b;}
u32 chain32(u32 a,u32 b) ABI {return h32(h32(a,b),0x13572468UL);}
u16 swap16(u16 a,u16 b) ABI {return h16(b,a);}
u16 live16(u16 a,u16 b) ABI {return (u16)(h16(a,b)+a+b);}
u8 swap8(u8 a,u8 b) ABI {return h8(b,a);}
u8 live8(u8 a,u8 b) ABI {return (u8)(h8(a,b)+a+b);}
u32 cycle816(u16 a,u8 b) ABI {return h816(b,a)+a+b;}
u32 cycle1632(u32 a,u16 b) ABI {return h1632(b,a)+a+b;}
u32 cycle328(u8 a,u32 b) ABI {return h328(b,a)+a+b;}
u32 sign8call(s8 a,s8 b) ABI {return (u32)(s32)hs8(b,a)+(u32)(s32)a;}
u32 sign16call(s16 a,s16 b) ABI {return (u32)(s32)hs16(b,a)+(u32)(s32)a;}
s16 sign16return(s16 a,s16 b) ABI {return hs16(b,a);}
u32 literal32(u32 a,u32 b) ABI {return h32(0xdeadbeefUL,a)+b;}
u8 literal8(u8 a,u8 b) ABI {return (u8)(h8(0xa5,a)+b);}
u32 same32(u32 a,u32 b) ABI {return h32(a,a)+b;}
u32 zeroargs(u32 a,u32 b) ABI {return hzero()+a+b;}
u32 voidcall(u32 a,u32 b) ABI {voidtail();clobber_void();return a^b;}
u32 discard(u32 a,u32 b) ABI {clobber(a,b);return a^b;}
u32 legacycallee(u32 a,u32 b) ABI {return old32(b,a)+a+b;}
u32 loopcall(u32 a,u32 b) ABI {u8 n;for(n=0;n<8;++n)a=h32(a,b);return a;}
u32 branchcall(u32 a,u32 b) ABI {if(a&1)return h32(a,b)+b;return h32(b,a)^a;}
u32 multiply(u32 a,u32 b) ABI {return a*b+b;}
u32 division(u32 a,u32 b) ABI {return a/(b|1UL)+b;}
u32 modulus(u32 a,u32 b) ABI {return a%(b|1UL)+b;}
u32 signeddivide(u32 a,u32 b) ABI {s32 x=(s32)a,y=(s32)(b|1UL);if(x==(-2147483647L-1L)&&y==-1)y=1;return (u32)(x/y);}
u32 spillcall(u32 a,u32 b) ABI {u32 v0=(a+0x01234567UL)^b,v1=(a+0x02468aceUL)^b,v2=(a+0x0369d035UL)^b,v3=(a+0x048d159cUL)^b,v4=(a+0x05b05b03UL)^b,v5=(a+0x06d3a06aUL)^b,v6=(a+0x07f6e5d1UL)^b,v7=(a+0x091a2b38UL)^b,v8=(a+0x0a3d709fUL)^b;u32 c=clobber(v8,v7);return c+((v0>>1)+(v1>>2)+(v2>>3)+(v3>>4)+(v4>>5)+(v5>>1)+(v6>>2)+(v7>>3)+(v8>>4))+(v0^v1^v2^v3^v4^v5^v6^v7^v8);}
u8 bytespillcall(u8 a,u8 b) ABI {u8 v0=(u8)(a+1),v1=(u8)(a+2),v2=(u8)(a+3),v3=(u8)(a+4),v4=(u8)(a+5),v5=(u8)(a+6),v6=(u8)(a+7),v7=(u8)(a+8),v8=(u8)(a+9),v9=(u8)(a+10),v10=(u8)(a+11),v11=(u8)(a+12),v12=(u8)(a+13),v13=(u8)(a+14),v14=(u8)(a+15),v15=(u8)(a+16),v16=(u8)(a+17),v17=(u8)(a+18);u8 c=h8(v17,v16);return (u8)(c^(v0>>(b&7))^(v1>>(b&7))^(v2>>(b&7))^(v3>>(b&7))^(v4>>(b&7))^(v5>>(b&7))^(v6>>(b&7))^(v7>>(b&7))^(v8>>(b&7))^(v9>>(b&7))^(v10>>(b&7))^(v11>>(b&7))^(v12>>(b&7))^(v13>>(b&7))^(v14>>(b&7))^(v15>>(b&7))^(v16>>(b&7))^(v17>>(b&7)));}
u32 spillargs(u32 a,u32 b) ABI {u32 v0=(a+0x01234567UL)^b,v1=(a+0x02468aceUL)^b,v2=(a+0x0369d035UL)^b,v3=(a+0x048d159cUL)^b,v4=(a+0x05b05b03UL)^b,v5=(a+0x06d3a06aUL)^b,v6=(a+0x07f6e5d1UL)^b,v7=(a+0x091a2b38UL)^b,v8=(a+0x0a3d709fUL)^b;u32 c0=clobber(v0,v1),c1=clobber(v2,v3),c2=clobber(v4,v5),c3=clobber(v6,v7),c4=clobber(v8,v0);return c0+c1+c2+c3+c4+v0+v1+v2+v3+v4+v5+v6+v7+v8;}
u8 bytespillargs(u8 a,u8 b) ABI {u8 v0=(u8)(a+1),v1=(u8)(a+2),v2=(u8)(a+3),v3=(u8)(a+4),v4=(u8)(a+5),v5=(u8)(a+6),v6=(u8)(a+7),v7=(u8)(a+8),v8=(u8)(a+9),v9=(u8)(a+10),v10=(u8)(a+11),v11=(u8)(a+12),v12=(u8)(a+13),v13=(u8)(a+14),v14=(u8)(a+15),v15=(u8)(a+16),v16=(u8)(a+17),v17=(u8)(a+18);u8 c0=h8(v0,v1),c1=h8(v2,v3),c2=h8(v4,v5),c3=h8(v6,v7),c4=h8(v8,v9),c5=h8(v10,v11),c6=h8(v12,v13),c7=h8(v14,v15),c8=h8(v16,v17);return (u8)(c0^c1^c2^c3^c4^c5^c6^c7^c8^v0^v1^v2^v3^v4^v5^v6^v7^v8^v9^v10^v11^v12^v13^v14^v15^v16^v17);}
u32 recursion(u32 a,u32 b) ABI {return recur(a,b&15);}
u32 recur(u32 a,u32 b) ABI {if(!b)return a;return recur(a+1,b-1)+a;}
void voidtail(void) ABI {hvoid();}
