#include "wide-pointer-objects.h"
u32 swap_pointer(P32 PTR *p,P32 q,u32 salt) OLD {u32 old=**p;*p=q;return old+salt;}
u32 read_pointer(P32 PTR *p) OLD {return **p;}
void set_bits(P32 PTR *p,u32 bits) OLD {*p=(P32)(UPTR)(bits&0xffffffUL);}
