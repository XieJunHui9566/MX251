typedef unsigned char u8;typedef signed char s8;
typedef unsigned int u16;typedef signed int s16;
typedef unsigned long u32;typedef signed long s32;
#define ABI __reentrant __sdcccall(4)
u8 legacy_u8(u32,u32) ABI;
s8 legacy_s8(u32,u32) ABI;
u8 r_unsigned(u32 a,u32 b) ABI {return (u8)(a^b);}
s8 r_signed(u32 a,u32 b) ABI {return (s8)(a+b);}
u32 r_truth(u32 a,u32 b) ABI {
 u8 x=(u8)a,y=(u8)b;u32 v=0;
 if(x)v|=1;if((s8)y)v|=2;
 return v|((u32)!x<<8)|((u32)!y<<16);
}
u32 r_bool16(u32 a,u32 b) ABI {
 u16 x=(u16)a,y=(u16)b;u8 p=x<y,q=(s16)x>(s16)y;
 return p|((u32)q<<8)|((u32)(x==y)<<16);
}
u32 r_bool32(u32 a,u32 b) ABI {
 u8 p=a<b,q=(s32)a>(s32)b;
 return p|((u32)q<<8)|((u32)(a==b)<<16);
}
u32 r_mixed(u32 a,u32 b) ABI {
 s16 x=(s16)a;u16 y=(u16)b;
 return (x<y)|((u32)(x>=y)<<8)|((u32)!(s8)a<<16);
}
u32 r_volatile(u32 a,u32 b) ABI {
 volatile u8 t[3];t[0]=(u8)a;t[1]=(u8)b;t[2]=0;
 if(t[0])t[2]=255;if(!t[1])t[2]=128;
 return ((u32)t[0]<<16)|((u32)t[1]<<8)|t[2];
}
u32 r_old_returns(u32 a,u32 b) ABI {
 u8 x=legacy_u8(a,b);s8 y=legacy_s8(b,a);
 return x|((u32)(u8)y<<8)|((u32)(y<0)<<16);
}
u32 r_new_returns(u32 a,u32 b) ABI {
 u8 x=r_unsigned(a,b);s8 y=r_signed(b,a);
 return x|((u32)(u8)y<<8)|((u32)(y<0)<<16);
}
u32 r_byte_assign(u32 a,u32 b) ABI {
 u8 x=(u8)a,y=(u8)b,z,p=x<y;
 z=0;if(p)z=0x80;else z=0xff;
 if(y)x=z;else x=y;
 return x|((u32)z<<8)|((u32)p<<16);
}
