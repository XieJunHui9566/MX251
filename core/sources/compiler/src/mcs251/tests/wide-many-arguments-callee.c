#include "wide-many-arguments.h"
#ifndef MANY_NAME
#define MANY_NAME many_mix
#endif
u32 MANY_NAME(MANY_PARAMETERS) ABI
{
    return ((u32)a << 1) ^ ((u32)b << 3) ^ (u32)(long)c ^
           ((u32)(u16)d << 7) ^ e ^ ((u32)f << 19) ^ ((u32)g << 11) ^
           (h >> 5) ^ ((u32)*p << 24) ^ ((u32)j << 9) ^
           ((u32)k << 15) ^ l ^ ((u32)*q << 17) ^
           ((u32)(long)n << 13) ^ ((u32)o << 2) ^ (last >> 3);
}
