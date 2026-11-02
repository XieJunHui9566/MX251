typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u8 uc_lt(u8 a,u8 b) ABI {return a<b;}
u8 uc_le(u8 a,u8 b) ABI {return a<=b;}
u8 uc_gt(u8 a,u8 b) ABI {return a>b;}
u8 uc_ge(u8 a,u8 b) ABI {return a>=b;}
u8 uc_eq(u8 a,u8 b) ABI {return a==b;}
u8 uc_ne(u8 a,u8 b) ABI {return a!=b;}
u8 ub_lt(u8 a,u8 b) ABI {if(a<b)return 0xa5;return 0x5a;}
u8 ub_le(u8 a,u8 b) ABI {if(a<=b)return 0xa5;return 0x5a;}
u8 ub_gt(u8 a,u8 b) ABI {if(a>b)return 0xa5;return 0x5a;}
u8 ub_ge(u8 a,u8 b) ABI {if(a>=b)return 0xa5;return 0x5a;}
u8 ub_eq(u8 a,u8 b) ABI {if(a==b)return 0xa5;return 0x5a;}
u8 ub_ne(u8 a,u8 b) ABI {if(a!=b)return 0xa5;return 0x5a;}
u8 sc_lt(s8 a,s8 b) ABI {return a<b;}
u8 sc_le(s8 a,s8 b) ABI {return a<=b;}
u8 sc_gt(s8 a,s8 b) ABI {return a>b;}
u8 sc_ge(s8 a,s8 b) ABI {return a>=b;}
u8 sc_eq(s8 a,s8 b) ABI {return a==b;}
u8 sc_ne(s8 a,s8 b) ABI {return a!=b;}
u8 sb_lt(s8 a,s8 b) ABI {if(a<b)return 0xa5;return 0x5a;}
u8 sb_le(s8 a,s8 b) ABI {if(a<=b)return 0xa5;return 0x5a;}
u8 sb_gt(s8 a,s8 b) ABI {if(a>b)return 0xa5;return 0x5a;}
u8 sb_ge(s8 a,s8 b) ABI {if(a>=b)return 0xa5;return 0x5a;}
u8 sb_eq(s8 a,s8 b) ABI {if(a==b)return 0xa5;return 0x5a;}
u8 sb_ne(s8 a,s8 b) ABI {if(a!=b)return 0xa5;return 0x5a;}
u8 band(u8 a,u8 b) ABI {return (u8)(a&b);}
u8 bor(u8 a,u8 b) ABI {return (u8)(a|b);}
u8 bxor(u8 a,u8 b) ABI {return (u8)(a^b);}
u8 bconst(u8 a,u8 b) ABI {return (u8)((a&0x81)^(b|0x7e));}
u8 breuse(u8 a,u8 b) ABI {return (u8)((a^b)&(a|0x35));}
u8 bsame(u8 a,u8 b) ABI {return (u8)((a^a)|(b&b));}
u8 b_touch(u8 __xdata *,u8) ABI;
u32 b_spill(u32 address,u32 x) ABI {u8 __xdata *p=(u8 __xdata *)address;u8 v0=p[0],v1=p[1],v2=p[2],v3=p[3],v4=p[4],v5=p[5],v6=p[6],v7=p[7],v8=p[8],v9=p[9],v10=p[10],v11=p[11],v12=p[12],v13=p[13],v14=p[14],v15=p[15],v16=p[16],v17=p[17],v18=p[18],v19=p[19],v20=p[20],v21=p[21];u8 z=b_touch(p,(u8)x);u32 sum=z;sum+=(u32)(u8)(v0^v21);sum+=(u32)(u8)(v1&v20);sum+=(u32)(u8)(v2|v19);sum+=(u32)(u8)(v3^v18);sum+=(u32)(u8)(v4&v17);sum+=(u32)(u8)(v5|v16);sum+=(u32)(u8)(v6^v15);sum+=(u32)(u8)(v7&v14);sum+=(u32)(u8)(v8|v13);sum+=(u32)(u8)(v9^v12);sum+=(u32)(u8)(v10&v11);return sum+(v0<v21)+(v1>v20)+((s8)v2<(s8)v19);}
u32 b_volatile(u32 address,u32 x) ABI {volatile u8 __xdata *p=(volatile u8 __xdata *)address;u8 a=p[0],b=p[1];p[2]=a^b;return (u32)p[2]+(a<b)+(u32)(u8)(a&b)+(u32)(u8)(a|b);}
