typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;typedef signed long s32;
#define ABI __reentrant __sdcccall(4)
u32 addxor(u32 a,u32 b) ABI {return (a+b)^b;}
u32 subtract(u32 a,u32 b) ABI {return b-a;}
u32 logic(u32 a,u32 b) ABI {return ((a&0xa5c3f087UL)|(b&0x5a3c0f78UL))^(a&b);}
u16 sum16(u16 a,u16 b) ABI {return (a+b)^b;}
u8 sum8(u8 a,u8 b) ABI {return (u8)((a+b)^b);}
u32 sign8(u32 a,u32 b) ABI {return (u32)(s32)(s8)a+b;}
u32 sign16(u32 a,u32 b) ABI {return (u32)(s32)(s16)a+b;}
u32 sign16_direct(s16 a,u32 b) ABI {return (u32)a+b;}
u32 zero8(u32 a,u32 b) ABI {return (u32)(u8)a+b;}
u32 zero16(u32 a,u32 b) ABI {return (u32)(u16)a+b;}
u32 cmp_s32(u32 a,u32 b) ABI {return (u32)((s32)a<(s32)b);}
u32 cmp_u32(u32 a,u32 b) ABI {return (u32)(a>=b);}
u32 cmp_s16(s16 a,s16 b) ABI {if(a>b)return 0x11223344UL;if(a==b)return 0x87654321UL;return 0xfedcba98UL;}
u32 cmp_u16(u16 a,u16 b) ABI {if(a<=b)return a;if(a!=b)return b;return 7;}
u32 cmp_s8(s8 a,s8 b) ABI {if(a>=b)return (u32)(s32)a;return (u32)(s32)b;}
u32 cmp_u8(u8 a,u8 b) ABI {return a<b?(u32)a:(u32)b;}
u32 left32(u32 a,u32 b) ABI {return a<<(b&31);}
u32 right32(u32 a,u32 b) ABI {return a>>(b&31);}
u32 right_s32(u32 a,u32 b) ABI {return (u32)((s32)a>>(b&31));}
u16 left16(u16 a,u16 b) ABI {return (u16)(a<<(b&15));}
u16 right16(u16 a,u16 b) ABI {return (u16)(a>>(b&15));}
u16 right_s16(s16 a,u16 b) ABI {return (u16)(a>>(b&15));}
u8 left8(u8 a,u8 b) ABI {return (u8)(a<<(b&7));}
u8 right8(u8 a,u8 b) ABI {return (u8)(a>>(b&7));}
u8 right_s8(s8 a,u8 b) ABI {return (u8)(a>>(b&7));}
u32 shift_const(u32 a,u32 b) ABI {return ((a<<16)|(b>>16))^((a>>31)+(b<<31));}
u32 select_s32(u32 a,u32 b) ABI {return (s32)a<(s32)b?a:b;}
u32 pressure(u32 a,u32 b) ABI {u32 c=a+b,d=a^b,e=a-b,f=(a<<3)+b;return ((c^d)+(e^f))^((c>>2)+(d<<4));}
u32 spill(u32 a,u32 b) ABI {u32 v0=(a+0x01234567UL)^b,v1=(a+0x02468aceUL)^b,v2=(a+0x0369d035UL)^b,v3=(a+0x048d159cUL)^b,v4=(a+0x05b05b03UL)^b,v5=(a+0x06d3a06aUL)^b,v6=(a+0x07f6e5d1UL)^b,v7=(a+0x091a2b38UL)^b,v8=(a+0x0a3d709fUL)^b;return ((v0>>1)+(v1>>2)+(v2>>3)+(v3>>4)+(v4>>5)+(v5>>1)+(v6>>2)+(v7>>3)+(v8>>4))^(v0^v1^v2^v3^v4^v5^v6^v7^v8);}
u32 loop(u32 a,u32 b) ABI {u8 n=(u8)b&15;while(n){if(a&1)a=(a<<1)^b;else a=(a>>1)+b;--n;}return a;}
u32 crc(u32 a,u32 b) ABI {u8 n;for(n=0;n<8;++n)a=(a<<1)^((a&0x80000000UL)?0x04c11db7UL:0UL);return a^b;}
u32 join(u32 a,u32 b) ABI {u32 x=a;if(a>b)x=b+17UL;else x=a^0xff00ff00UL;if(x&1)x+=a;else x^=b;return x+a;}
u32 mixed(u32 a,u32 b) ABI {u8 c=(u8)a;u16 d=(u16)b;u32 e=a^b;return (e+(u32)(s32)(s8)c)^((u32)d+(a>>8));}
u8 byte_spill(u8 a,u8 b) ABI {u8 v0=(u8)(a+1),v1=(u8)(a+2),v2=(u8)(a+3),v3=(u8)(a+4),v4=(u8)(a+5),v5=(u8)(a+6),v6=(u8)(a+7),v7=(u8)(a+8),v8=(u8)(a+9),v9=(u8)(a+10),v10=(u8)(a+11),v11=(u8)(a+12),v12=(u8)(a+13),v13=(u8)(a+14),v14=(u8)(a+15),v15=(u8)(a+16),v16=(u8)(a+17),v17=(u8)(a+18);return (u8)((v0>>(b&7))^(v1>>(b&7))^(v2>>(b&7))^(v3>>(b&7))^(v4>>(b&7))^(v5>>(b&7))^(v6>>(b&7))^(v7>>(b&7))^(v8>>(b&7))^(v9>>(b&7))^(v10>>(b&7))^(v11>>(b&7))^(v12>>(b&7))^(v13>>(b&7))^(v14>>(b&7))^(v15>>(b&7))^(v16>>(b&7))^(v17>>(b&7)));}
