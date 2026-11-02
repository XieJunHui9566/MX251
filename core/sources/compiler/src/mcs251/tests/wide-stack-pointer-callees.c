#include "wide-stack-pointers.h"
/* 整数往返只检查本端既有的平坦 24 位地址表示，不解引用这些测试地址。 */
u32 ptr3(u32 a,u32 b,u8 X *p) ABI {return (a+b)^(u32)p;}
u32 ptr8(u32 a,u32 b,u8 c,u8 X *p,u16 d,u32 X *q,s8 e,u8 X *r) ABI {
  return (a^b)+(u32)c+(u32)p+((u32)d<<8)+(u32)q+(u32)e+(u32)r;
}
u32 many_ptr(u8 X *a,u8 X *b,u8 X *c,u8 X *d,u8 X *e,u8 X *f,u8 X *g,u8 X *h) ABI {
  return (u32)a+((u32)b^0x87654321UL)+(u32)c+(u32)d+(u32)e+(u32)f+(u32)g+(u32)h;
}
u8 X *retptr(u32 a,u32 b,u8 X *p,u16 offset) ABI {return p+offset;}
u32 narrowptr(u32 a,u32 b,u8 X *p) ABI {return ((u32)(u8)p<<24)^((u32)(u16)p<<8)^a^b;}
u32 recurseptr(u32 a,u32 n,u8 X *p,u8 X *q) ABI {
  if(!n)return a+*p+((u32)*q<<8);
  return recurseptr(a+*p,n-1,q,p)+a;
}
u32 change3(u32 a,u32 b,u8 X *p,u16 offset,u32 tail) ABI {
  p[offset]=(u8)(a^b);return (a+b)^tail^((u32)p[offset]<<16);
}
u32 generic3(u32 a,u32 b,u8 *p,u16 offset) ABI {return a^b^p[offset];}
u32 volatile3(u32 a,u32 b,volatile u8 X *p,u32 tail) ABI {
  u8 first=*p;u8 second=*p;return (a^b)+tail+first+((u32)second<<8);
}
