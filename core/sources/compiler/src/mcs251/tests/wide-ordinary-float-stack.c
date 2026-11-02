#include "wide-ordinary-calls.h"
u32 select_bits(u32 a,u32 b) OLD
{Bits x,y,z;x.u=a;y.u=b;z.f=old_float(x.f,y.f,(u8)(a&1U));return z.u;}
u32 volatile_bits(u32 a,u32 b) OLD
{volatile Bits x,y,z;x.u=a;y.u=b;z.f=old_float(x.f,y.f,(u8)(a&1U));return z.u;}
u32 alias_bits(u32 a,u32 b) OLD
{Bits x,y;x.u=a;mutate(&x.u,&x.u,b);y.f=old_float(x.f,x.f,(u8)b);return y.u;}
u32 guarded_bits(u32 a,u32 b) OLD
{struct {u32 before;Bits value;u32 after;} x;x.before=0x13579bdfUL;x.value.u=a;x.after=0x2468ace0UL;
 x.value.f=old_float(x.value.f,x.value.f,(u8)b);return x.value.u^x.before^x.after;}
u32 pressure_bits(u32 a,u32 b) OLD
{u32 v0=a+1UL,v1=a+0x12345678UL,v2=b^0xa5a5a5a5UL,v3=b+0x10001UL;
 u32 v4=a^0xdeadbeefUL,v5=a+b,v6=a-b,v7=b+0x87654321UL;Bits x,y,z;
 x.u=a;y.u=b;z.f=old_float(x.f,y.f,(u8)a);return z.u+v0+v1+v2+v3+v4+v5+v6+v7;}
u32 adjacent_bits(u32 a,u32 b) OLD
{struct {u8 before;Bits value;u8 after;} x;Bits y;x.before=0x5aU;x.value.u=a;x.after=0xa5U;y.u=b;
 x.value.f=old_float(x.value.f,y.f,(u8)b);return x.value.u+x.before+x.after;}
