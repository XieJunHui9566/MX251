#include "wide-ordinary.h"
u8 PTR *literal_pointer(u8 PTR *p) OLD;
#if defined(__SDCC) || defined(__C251__)
#define PROBE_PTR ((u8 PTR *)0x123456UL)
#else
#define PROBE_PTR ((u8 *)(uintptr_t)0x123456UL)
#endif
u32 native_leaf(u32 a,u32 b) ABI {return (a+b)^b;}
float float_bridge(float a,float b,u8 choose) ABI {return choose?float_value(1.5f,b,1):float_value(-2.0f,a,1);}
u32 c_zero(u32 a,u32 b) ABI {return zero()+a+b+(u32)byte_value((s8)-128,b)+(u32)word_value((s16)-32768,(u8)b)+dword_value(0x89abcdefUL,b);}
u32 c_byte(u32 a,u32 b) ABI {return (u32)byte_value((s8)a,b)^a;}
u32 c_word(u32 a,u32 b) ABI {return (u32)word_value((s16)a,(u8)b)^b;}
u32 c_dword(u32 a,u32 b) ABI {return dword_value(a,b);}
u32 c_pointer(u32 a,u32 b) ABI
{struct {u8 x,y;} local;u8 PTR *p;local.x=(u8)a;local.y=(u8)b;p=pointer_value(&local.x,b,&local.y);return *p+a+b+(literal_pointer(PROBE_PTR)==PROBE_PTR?0x10000UL:0UL);}
u32 c_float(u32 a,u32 b) ABI
{Bits x,y,z;x.u=a;y.u=b;z.f=float_bridge(x.f,y.f,(u8)(a&1U));return z.u^a^b;}
u32 c_many(u32 a,u32 b) ABI
{struct {u8 x;} local;local.x=(u8)(a^b);return many((u8)a,(u16)b,a,(s8)b,&local.x,(s16)a,b,(u8)(a>>24));}
u32 c_store(u32 a,u32 b) ABI
{struct {u32 before,value,after;} x;x.before=0x13579bdfUL;x.after=0x2468ace0UL;store_value(&x.value,a,b);return x.value^(x.before^0x13579bdfUL)^(x.after^0x2468ace0UL);}
u32 c_chain(u32 a,u32 b) ABI {return chain(a,b)^b;}
u32 c_pressure(u32 a,u32 b) ABI
{
 u32 v0=a+1UL,v1=a+0x12345678UL,v2=b^0xa5a5a5a5UL,v3=b+0x10001UL;
 u32 v4=a^0xdeadbeefUL,v5=a+b,v6=a-b,v7=b+0x87654321UL;
 struct {u8 x;} local;u32 v;local.x=(u8)a;
 v=many((u8)v0,(u16)v1,v2,(s8)v3,&local.x,(s16)v4,v5,(u8)v6);
 return v+v0+v1+v2+v3+v4+v5+v6+v7;
}

