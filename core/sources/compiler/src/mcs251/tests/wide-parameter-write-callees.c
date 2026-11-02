#include "wide-parameter-write.h"
u32 p_byte(u32 a,u32 b,u8 c,u32 guard) ABI
{c=(u8)legacy_value(a,b);c^=(u8)b;return (u32)c+guard+a;}
u32 p_word(u32 a,u32 b,s16 c,u8 guard) ABI
{c=(s16)(b^0x80008000UL);c=(s16)(c^(s16)a);return (u32)c^guard^b;}
u32 p_dword(u32 a,u32 b,u8 guard,u32 c,u16 tail) ABI
{c=legacy_value(c,b);c+=a;return c^guard^tail;}
u32 p_pointer(u32 a,u32 b,u8 left,u8 PTR *p,u16 right,u8 PTR *q) ABI
{u8 old=*p;p=q;if(b&1U)++p;return (u32)*p+old+left+right+a;}
u32 p_address(u32 a,u32 b,u8 left,u8 PTR *p,u16 right,u8 PTR *q,u32 guard,u8 last) ABI
{u32 before=(u32)(uaddr)p;p=q;if(b&1U)p=(u8 PTR *)(uaddr)(a&0xffffffUL);return before^(u32)(uaddr)p^(u32)(uaddr)q^guard^left^right^last;}
float p_float(u32 a,u32 b,u8 guard,float x,u16 tail,float y) ABI
{if((a^guard^tail)&1U)x=y;else y=x;if(b&1U)y=x;return y;}
u32 p_eight(u32 a,u32 b,u8 c,u16 d,u32 e,u8 f,u16 g,u32 h) ABI
{c+=(u8)b;d^=(u16)a;e=legacy_value(e,b);f^=c;g+=d;h+=e;return c+(u32)d+e+f+(u32)g+h+a;}
u32 p_loop(u32 a,u32 b,u8 n,u32 c,u16 d) ABI
{while(n){c=(c<<1)^(b+n);d+=(u16)c;--n;}return c+(u32)d+a;}
u32 p_recurse(u32 a,u32 b,u8 n,u32 c,u16 guard) ABI
{c=legacy_value(c,b);if(n){--n;c=p_recurse(a+1,b^c,n,c,guard);}return c+a+guard;}
u32 p_frame(u32 a,u32 b,u8 guard,u32 c,u16 tail) ABI
{struct {u32 first;u8 bytes[9];u32 last;} local;u8 i;local.first=a;local.last=b;
 for(i=0;i<9;++i){local.bytes[i]=(u8)(c+i);}c=legacy_value(c,b);
 for(i=0;i<9;++i){c+=local.bytes[i];}return c+local.first+local.last+guard+tail;}
u32 p_pressure(u32 a,u32 b,u8 guard,u32 c,u16 tail) ABI
{u32 v0=a+1UL,v1=a+0x12345678UL,v2=b^0xa5a5a5a5UL,v3=b+0x10001UL;
 u32 v4=a^0xdeadbeefUL,v5=a+b,v6=a-b,v7=b+0x87654321UL;
 c=legacy_value(c,b);c+=v0+v1+v2+v3+v4+v5+v6+v7;return c+guard+tail;}
