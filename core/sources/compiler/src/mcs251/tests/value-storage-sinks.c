#include "value-storage.h"
#include <stdarg.h>

u32 vs_visit(u8 *p,u8 length,u16 seed)
{
    u8 i;
    u32 result=0x13579bdfUL;
    for(i=0;i<length;++i) {
        result=(result<<3)^result^p[i];
        p[i]+=(u8)(seed+i);
    }
    return result;
}

u32 vs_three(u8 seed,u8 *p,u8 *q,u8 *r,u16 word)
{
    *p^=seed;
    *q+=(u8)word;
    *r^=(u8)(word>>8);
    return ((u32)*p<<16)^((u16)*q<<8)^*r;
}

u32 vs_variadic(u16 seed,...)
{
    va_list args;
    u8 *p,*q;
    u16 word;
    va_start(args,seed);
    p=va_arg(args,u8 *);
    word=va_arg(args,u16);
    q=va_arg(args,u8 *);
    va_end(args);
    *p+=(u8)seed;
    *q^=(u8)word;
    return ((u32)*p<<24)^((u16)*q<<8)^word;
}
