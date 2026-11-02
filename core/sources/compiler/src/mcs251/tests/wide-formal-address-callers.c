#include "wide-formal-address.h"
u32 c_first(u32 a,u32 b) ABI {u32 z=write32(&a,b);a+=b;return a^z;}
u32 c_second(u32 a,u32 b) ABI {u32 z=write32(&b,a);b^=a;return b+z;}
u32 formal_third(u32 a,u32 b,u32 c) ABI {u32 z=write32(&c,a);c^=b;return a+b+c+z;}
u32 c_third(u32 a,u32 b) ABI {return formal_third(a,b,a^b);}
u32 formal_mixed(u8 a,u16 b,u8 c,s16 d,u8 PTR *p,u32 f,u8 g,u32 h) ABI {
 u32 z=write8(&a,b);z^=write_u16(&b,f);z+=write8(&c,h);
 z^=write16(&d,f);z+=write32(&h,f);c+=(u8)b;h^=b;
 return z+a+b+c+(u32)d+p[0]+f+g+h;
}
u32 c_mixed(u32 a,u32 b) ABI {u8 local[3];local[0]=(u8)(a^b);local[1]=0x5a;local[2]=0xa5;
 return formal_mixed((u8)a,(u16)b,(u8)(a>>8),(s16)(b>>16),local,a,(u8)b,b)+local[1]+local[2];
}
u32 c_alias(u32 a,u32 b) ABI {u32 initial=b;write_alias(&a,(b&1U)?&a:&b,b);a+=initial;return a^b;}
float formal_float(float a,float b,float c,u32 bits) ABI {
 u32 x=read_float(&a)^read_float(&b)^read_float(&c)^bits;write_float(&a,x);return a;
}
u32 c_float(u32 a,u32 b) ABI {Bits x,y,z,result;x.u=a;y.u=b;z.u=a+b;
 result.f=formal_float(x.f,y.f,z.f,b^0x12345678UL);return result.u;}
u32 c_loop(u32 a,u32 b) ABI {u32 sum=0;u8 n;for(n=0;n<(u8)(b&7U);++n){sum+=write32(&a,b+n);a+=n;}return a^sum;}
u32 formal_recurse(u32 a,u32 b,u8 n) ABI {u32 z=write32(&a,b);if(n){z+=formal_recurse(a,b+1,n-1);}return a^z;}
u32 c_recurse(u32 a,u32 b) ABI {return formal_recurse(a,b,(u8)(b&3U));}
u32 c_frame(u32 a,u32 b) ABI {u8 guard[17];u32 z;guard[0]=(u8)a;guard[16]=(u8)b;
 z=write32(&a,b);z^=write32(&b,a);return a^b^z^guard[0]^guard[16];}
u32 formal_pressure(u32 a,u32 b,u32 c) ABI {u32 v0=a+1UL,v1=a+0x12345678UL,v2=b^0xa5a5a5a5UL,v3=b+0x10001UL;
 u32 v4=a^0xdeadbeefUL,v5=a+b,v6=a-b,v7=b+0x87654321UL,z;
 z=write32(&c,b);c+=v0+v1+v2+v3+v4+v5+v6+v7;return c^z;}
u32 c_pressure(u32 a,u32 b) ABI {return formal_pressure(a,b,a^b);}
