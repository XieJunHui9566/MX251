#include "wide-ordinary-calls.h"
u32 native_immutable(u32 a,u32 b) ABI {return (a+b)^b;}
u32 native_calls(u32 a,u32 b) ABI {return old_pair(b,a)+old_pair(a,b)+a+b;}
s16 native_words(s16 a,u16 b) ABI {return (s16)((u16)a^b);}
u8 native_bytes(u8 a,u8 b) ABI {return (u8)(a+b)^b;}
float native_float_bits(float a,float b,u8 select) ABI {return select?a:b;}
u32 PTR *native_choose(u32 PTR *p,u32 PTR *q,u8 select) ABI {return select?p:q;}
u32 native_mutable(u32 a,u32 b,u8 n) ABI {while(n--){a+=b;b^=a;}return a^b;}
u32 native_addressed(u32 a,u32 b) ABI {mutate(&a,&b,a);return a+b;}
u32 native_neighbor(u32 a,u32 b) ABI {volatile u8 guard[3];guard[0]=(u8)a;guard[2]=(u8)b;return a+b+guard[0]+guard[2];}
u32 native_pointee(volatile u32 PTR *p,volatile u32 PTR *q) ABI {*p=*q+1;return *p^*q;}
