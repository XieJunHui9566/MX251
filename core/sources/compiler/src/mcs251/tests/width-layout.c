/* 不同位宽同时存活，覆盖进位、截断、跨文件调用及局部对象别名。 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
extern u32 width_mix(u32, u16, u8) __reentrant;
extern void width_touch(u16 *, u8) __reentrant;

u32 width_bytes(u32 x, u32 y) __reentrant {
  u8 a=x, b=x>>8, c=y, d=y>>8, e=x>>16, f=y>>24;
  u32 z=(x+y)^((u32)(u8)(a+c)<<24);
  return z^((u32)(u8)(b-d)<<16)^((u32)(u8)(e+f)<<8)^(u8)(a^b^c^d^e^f);
}
u32 width_words(u32 x, u32 y) __reentrant {
  u16 a=x, b=x>>16, c=y, d=y>>16;
  u32 z=x-y;
  return z^((u32)(u16)(a+c)<<16)^(u16)(b-d)^((u32)a*d)^((u32)b*c);
}
u32 width_carry(u32 x, u32 y) __reentrant {
  u16 a=x, b=y;
  a+=126; b-=129;
  return ((u32)a<<16)|b;
}
u32 width_loop(u32 x, u32 y) __reentrant {
  u8 k, c=x>>24; u16 w=y;
  for(k=0;k<9;++k) {
    x=(x+y)^(x<<3); y=(y-x)^(y>>5);
    c+=(u8)x; w+=(u16)(y+c);
  }
  return x^y^((u32)w<<8)^c;
}
u32 width_call(u32 x, u32 y) __reentrant {
  u32 a=x^0x12345678UL,b=y+0x9abcdef0UL,c=x-y;
  u16 w=x+y; u8 n=x>>13;
  u32 z=width_mix(a+b,w,n);
  return z^a^b^c^((u32)w<<16)^n;
}
u32 width_cast(u32 x, u32 y) __reentrant {
  signed char a=(signed char)x; signed int b=(signed int)y;
  u32 z=(u32)(signed long)a+(u32)(signed long)b;
  return z^(x+y)^((u32)(u16)((signed long)a+b)<<8);
}
u32 width_array(u32 x, u32 y) __reentrant {
  u16 a[4];u8 i=(u8)(x^y)&3;
  a[0]=x;a[1]=x>>16;a[2]=y;a[3]=y>>16;
  width_touch(a,i);
  return ((u32)(u16)(a[0]+a[2])<<16)^(u16)(a[1]+a[3])^a[i];
}
u32 width_compare(u32 x, u32 y) __reentrant {
  u16 a=x,b=y;u8 c=x>>16,d=y>>16;
  u32 z=x<y?x-y:x+y;
  if(a<b)z^=0xa55a00ffUL;
  if(c==d)z^=0xdeadbeefUL;
  if((signed int)a<(signed int)b)z+=0x76543210UL;
  return z;
}
u32 width_rel32(u32 x, u32 y) __reentrant {
  return (u32)(x<y)|((u32)(x>y)<<1)|((u32)(x<=y)<<2)|((u32)(x>=y)<<3)|((u32)(x==y)<<4);
}
u32 width_rel16(u32 x, u32 y) __reentrant {
  u16 a=x,b=y;
  return (u32)(a<b)|((u32)(a>b)<<1)|((u32)(a<=b)<<2)|((u32)(a>=b)<<3)|((u32)(a==b)<<4);
}
