typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u8 b_touch(u8 __xdata *,u8) ABI;
u32 b_pressure(u32 address,u32 x) ABI {u8 __xdata *p=(u8 __xdata *)address;u8 v0=p[0],v1=p[1],v2=p[2],v3=p[3],v4=p[4],v5=p[5],v6=p[6],v7=p[7],v8=p[8],v9=p[9],v10=p[10],v11=p[11],v12=p[12],v13=p[13],v14=p[14],v15=p[15],v16=p[16],v17=p[17],v18=p[18],v19=p[19],v20=p[20],v21=p[21];u8 c,q,s;v0^=v11;v1&=v12;v2|=v13;v3^=v14;v4&=v15;v5|=v16;v6^=v17;v7&=v18;v8|=v19;v9^=v20;v10&=v21;c=(v3<v7)+((s8)v4<(s8)v8);q=b_touch(p,(u8)x);s=q;s^=v0;s^=v1;s^=v2;s^=v3;s^=v4;s^=v5;s^=v6;s^=v7;s^=v8;s^=v9;s^=v10;s^=v11;s^=v12;s^=v13;s^=v14;s^=v15;s^=v16;s^=v17;s^=v18;s^=v19;s^=v20;s^=v21;return ((u32)c<<8)|s;}
u32 b_constants(u8 a,u8 b) ABI {u32 v=0;if(a<31)v|=1;if(a>=128)v|=2;if(a==255)v|=4;if((s8)b< -1)v|=8;if((s8)b>0)v|=16;if((s8)b==127)v|=32;return v;}
