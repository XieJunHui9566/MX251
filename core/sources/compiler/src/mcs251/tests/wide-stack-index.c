typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#define ABI __reentrant __sdcccall(4)
u32 legacy_mutate(u8 *p,u32 value) ABI;

u32 si_u8(u32 x,u32 n) ABI {
  u8 a[32],i;u32 h=x;
  for(i=0;i<32;++i)a[i]=(u8)(x+i);
  for(i=0;i<32;++i){u8 j=(u8)(n+i)&31;a[j]^=i;h+=a[j];}
  return h;
}
u32 si_s8(u32 x,u32 n) ABI {
  u8 a[33],i,*p=a+16;s8 j=(s8)(n&31)-16;
  for(i=0;i<33;++i)a[i]=(u8)(x+3U*i);
  p[(s32)j]^=0xa5;return (u32)p[(s32)j]+a[0]+a[32];
}
u32 si_s16(u32 x,u32 n) ABI {
  u8 a[280];u16 i;s16 j=(s16)(n&255)-128;u8 *p=a+128;
  for(i=0;i<280;++i)a[i]=(u8)(x+i);
  return (u32)p[(s32)j]+a[279];
}
u32 si_s32(u32 x,u32 n) ABI {
  u32 a[8];u8 i;s32 j=(s32)(n&7)-4;u32 *p=a+4;
  for(i=0;i<8;++i)a[i]=x+i;
  p[j]^=n;return p[j]+a[7];
}
u32 si_sub(u32 x,u32 n) ABI {
  u8 a[32],i,*end=a+32;u16 j=(u16)(n&31)+1;
  for(i=0;i<32;++i)a[i]=(u8)(x+i);
  return *(end-j)+(u32)a[0];
}
u32 si_merge(u32 x,u32 n) ABI {
  u8 a[16],b[16],i,*p;u32 h=0;
  for(i=0;i<16;++i){a[i]=(u8)(x+i);b[i]=(u8)(x-i);}
  if(n<0x80000000UL)p=a;else p=b;
  for(i=0;i<16;++i)h+=p[i];
  return h;
}
u32 si_call(u32 x,u32 n) ABI {
  u8 a[32],i;u32 h=x;
  for(i=0;i<32;++i)a[i]=(u8)(x+i);
  for(i=0;i<8;++i){u8 j=(u8)(n+i)&31;h+=legacy_mutate(a,n+i);h+=a[j];}
  return h+a[0]+a[31];
}
u32 si_volatile(u32 x,u32 n) ABI {
  volatile u16 a[8];u8 i,j=(u8)n&7;u32 h=0;
  for(i=0;i<8;++i)a[i]=(u16)(x+i);
  a[j]^=(u16)n;
  for(i=0;i<8;++i)h+=a[i];
  return h;
}
u32 si_nested(u32 x,u32 depth) ABI {
  u8 a[16],i;u32 h=0;
  for(i=0;i<16;++i)a[i]=(u8)(x+i);
  if(depth)h=si_nested(x+3,depth-1);
  for(i=0;i<16;++i)h+=a[i];
  return h;
}
u32 si_two(u32 x,u32 n) ABI {
  u8 a[5],b[7],i;u32 c[3];
  for(i=0;i<5;++i)a[i]=(u8)(x+i);
  for(i=0;i<7;++i)b[i]=(u8)(x-i);
  for(i=0;i<3;++i)c[i]=x+i;
  a[n&3]^=b[n&3];c[n&1]+=a[n&3];
  return c[0]^c[1]^c[2]^a[n&3];
}
