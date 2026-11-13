#include "byte-pack.h"

u32 pack_be(const u8 *p)
{return ((u32)p[0]<<24)+((u32)p[1]<<16)+(u32)p[2]*256+p[3];}
u32 pack_le(const u8 *p)
{return p[0]+((u32)p[1]<<8)+((u32)p[2]<<16)+((u32)p[3]<<24);}
u16 pack_word(const u8 *p)
{return ((u16)p[0]<<8)|p[1];}
u32 pack_or(const u8 *p)
{return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
u32 pack_xor(const u8 *p)
{return ((u32)p[0]<<24)^((u32)p[1]<<16)^((u32)p[2]<<8)^p[3];}
u32 pack_volatile(const volatile u8 *p)
{return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
u32 pack_repeat(u8 a,u8 b)
{return ((u32)a<<24)+((u32)a<<16)+((u32)b<<8)+b;}
u32 pack_gaps(u8 a,u8 b)
{return ((u32)a<<24)|0x005a0000UL|((u32)b<<8);}

/* Full-width sign extension, overlapping byte lanes, shared intermediate,
 * and truncation before later extension are distinct C operations. */
u32 pack_signed(const s8 *p)
{return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3];}
u32 pack_overlap(const u8 *p)
{return ((u32)p[0]<<8)+((u32)p[1]<<8);}
u32 pack_shared(const u8 *p)
{u32 a=(u32)p[0]<<16;return (a+p[1])^a;}
u32 pack_truncated(const u8 *p)
{return ((u32)(u16)((u32)p[0]<<16))|((u32)p[1]<<8)|p[2];}
u32 pack_pressure(u32 a,u32 b,const u8 *p)
{
    u32 x=a+0x12345678UL,y=b^0x87654321UL,z=a-b,w=a+b;
    u32 v=((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
    return v+x+(y^z)+w;
}
u32 pack_native(u32 tag,u8 a,u8 b,u8 c,u8 d) __reentrant __sdcccall(4)
{return tag^(((u32)a<<24)|((u32)b<<16)|((u32)c<<8)|d);}
