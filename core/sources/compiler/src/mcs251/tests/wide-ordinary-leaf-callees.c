#include "wide-ordinary-leaf.h"
u32 c_first(u32 a,u32 b) OLD {u32 PTR *p=(b&1)?&a:&b;*p+=0x12345678UL;return a^b;}
u32 c_second(u32 a,u32 b) OLD {volatile u32 PTR *p=&b;u32 z=*p;*p=a^b;return z+b;}
u32 c_alias(u32 a,u32 b) OLD {u32 PTR *p=(b&1)?&a:&b;*p^=a;return a+b;}
u32 c_local(u32 a,u32 b) OLD {u32 x=a,y=b;u32 PTR *p=(b&1)?&x:&y;*p+=b;return x^y;}
u32 leaf_pointer(P32 p,P32 q,u32 choose) OLD {P32 PTR *pp=(choose&1)?&p:&q;*pp=q;return *p^*q;}
u32 leaf_bits(P32 p,P32 q,u32 choose) OLD {P32 PTR *pp=(choose&1)?&p:&q;*pp=p;return (u32)(UPTR)p^(u32)(UPTR)q;}
u32 c_unused(u32 a,u32 b) OLD {(void)a;return b^0x87654321UL;}
void leaf_frame(P32 p,u32 a,u32 b) OLD {volatile u32 local[2];local[0]=a;local[1]=b;*p=(local[0]+local[1])^0x12345678UL;}
float leaf_negative_zero(void) OLD {return -0.0f;}
u32 c_pressure(u32 a,u32 b) OLD {
 u32 v0=a+1UL,v1=a+0x12345678UL,v2=b^0xa5a5a5a5UL,v3=b+0x10001UL;
 u32 v4=a^0xdeadbeefUL,v5=a+b,v6=a-b,v7=b+0x87654321UL;
 u32 PTR *p=(b&1)?&a:&b;*p^=0x87654321UL;
 return a+b+v0+v1+v2+v3+v4+v5+v6+v7;
}
