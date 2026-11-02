#include "wide-ordinary-leaf.h"
u32 c_ptr(u32 a,u32 b) OLD {u32 x=a,y=b;return leaf_pointer(&x,&y,b);}
u32 c_bits(u32 a,u32 b) OLD {return leaf_bits((P32)(UPTR)(a&0xffffffUL),(P32)(UPTR)(b&0xffffffUL),b);}
u32 c_frame(u32 a,u32 b) OLD {u32 x=0;leaf_frame(&x,a,b);return x;}
u32 c_float(u32 a,u32 b) OLD {union {u32 u;float f;} v;v.f=leaf_negative_zero();return v.u^a^b;}
