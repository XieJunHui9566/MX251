#include "wide-local-scalar.h"
u32 c_byte(u32 a,u32 b) ABI {u8 x=(u8)a;u32 z=write8(&x,b);x+=(u8)a;return x+z;}
u32 c_word(u32 a,u32 b) ABI {s16 x=(s16)a,before=x;u32 z=write16(&x,b);x=(s16)(x^before);return (u32)x^z;}
u32 c_dword(u32 a,u32 b) ABI {u32 x=a,z=write32(&x,b);x^=a;return x^z;}
u32 c_alias(u32 a,u32 b) ABI {u32 x=a,y=b;write_alias(&x,(b&1U)?&x:&y,b);return x^y;}
float local_float(u32 a,u32 b) ABI {float x;write_float(&x,a);write_float(&x,b);return x;}
u32 c_float(u32 a,u32 b) ABI {Bits result;result.f=local_float(a,b);return result.u;}
u32 c_volatile(u32 a,u32 b) ABI {volatile u32 x=a;u32 first=x;write_volatile(&x,b);x+=first;return x^x;}
u32 c_loop(u32 a,u32 b) ABI {u32 x=a,sum=0;u8 n;for(n=0;n<(u8)(b&7U);++n){sum+=write32(&x,b+n);x+=n;}return x^sum;}
u32 local_recurse(u32 a,u32 b,u8 n) ABI {u32 x=a,z=write32(&x,b);if(n){z+=local_recurse(x,b+1,n-1);}return x^z;}
u32 c_recurse(u32 a,u32 b) ABI {return local_recurse(a,b,(u8)(b&3U));}
u32 c_frame(u32 a,u32 b) ABI {u32 before=0x13579bdfUL,x=a,after=0x2468ace0UL;u32 z;
 z=write32(&before,0);z^=write32(&x,b);z^=write32(&after,0);return x^before^after^z;}
u32 c_pressure(u32 a,u32 b) ABI {u32 v0=a+1UL,v1=a+0x12345678UL,v2=b^0xa5a5a5a5UL,v3=b+0x10001UL;
 u32 v4=a^0xdeadbeefUL,v5=a+b,v6=a-b,v7=b+0x87654321UL,x=a^b,z;
 z=write32(&x,b);x+=v0+v1+v2+v3+v4+v5+v6+v7;return x^z;}
