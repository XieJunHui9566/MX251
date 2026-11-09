#include "value-copy.h"

u32 copy_sink0(u16 a,u16 b,u8 octet,u8 mode,u32 bias)
{ return bias ^ ((u32)(u16)(a+3)<<16) ^ (u16)(b^(octet+11)) ^ ((u32)mode<<4); }
u32 copy_sink1(u16 a,u16 b,u8 octet,u8 mode,u32 bias)
{ return bias ^ ((u32)(u16)(a+5)<<16) ^ (u16)(b^(octet+13)) ^ ((u32)mode<<8); }
u32 copy_sink2(u16 a,u16 b,u8 octet,u8 mode,u32 bias)
{ return bias ^ ((u32)(u16)(a+7)<<16) ^ (u16)(b^(octet+17)) ^ ((u32)mode<<12); }
u32 copy_sink3(u16 a,u16 b,u8 octet,u8 mode,u32 bias)
{ return bias ^ ((u32)(u16)(a+9)<<16) ^ (u16)(b^(octet+19)) ^ ((u32)mode<<16); }
