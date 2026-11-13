#include "relative-address.h"

u32 address_forward(const u8 *p)
{return ((u32)p[4]<<24)|((u32)p[5]<<16)|((u32)p[6]<<8)|p[7];}
u32 address_reverse(const u8 *p)
{return ((u32)p[7]<<24)|((u32)p[6]<<16)|((u32)p[5]<<8)|p[4];}
u32 address_negative(const u8 *p)
{return ((u32)p[-4]<<24)|((u32)p[-3]<<16)|((u32)p[-2]<<8)|p[-1];}
u32 address_volatile(const volatile u8 *p)
{return ((u32)p[4]<<24)|((u32)p[5]<<16)|((u32)p[6]<<8)|p[7];}
u32 address_calls(const u8 *p)
{u32 a=p[4];return (a<<16)|((u32)address_read(p+5)<<8)|p[6];}
u32 address_branch(const u8 *p,u8 select)
{u32 a=p[4];if(select)a+=p[7];else a^=p[8];return (a<<8)|p[5];}
u32 address_scratch(const u8 *p,u32 value)
{u32 a=p[4];return (a*value)^((u32)p[5]<<8)^p[6];}
u32 address_changed(const u8 *p,u8 offset)
{u32 a=p[4];p+=offset;return (a<<16)|((u32)p[5]<<8)|p[6];}
void address_store(u8 *p)
{
    p[4]=address_tag>>24;p[5]=address_tag>>16;
    p[6]=address_tag>>8;p[7]=address_tag;
}
