#include "wide-local-scalar.h"
u32 write8(u8 PTR *p,u32 b) OLD {u32 old=*p;*p=(u8)(old^b);return old+b;}
u32 write16(s16 PTR *p,u32 b) OLD {*p=(s16)(*p^(s16)b);return (u32)*p;}
u32 write32(u32 PTR *p,u32 b) OLD {u32 old=*p;*p=(old^b)+0x87654321UL;return old+b;}
void write_alias(u32 PTR *p,u32 PTR *q,u32 b) OLD {*p+=b;*q^=*p;}
void write_volatile(volatile u32 PTR *p,u32 b) OLD {*p=(*p^b)+1UL;}
void write_float(float PTR *p,u32 b) OLD {Bits v;v.u=b;*p=v.f;}
