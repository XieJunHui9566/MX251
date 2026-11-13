#include "indexed-address.h"

u8 index_twice(const u8 *p,u16 i) { return p[(u16)(i*2U)]; }
u8 index_four(const u8 *p,u16 i) { return p[(u16)(i<<2)]; }
u8 index_code_byte(const u8 __code *p,u8 i) { return p[i*2U]; }
u16 index_code_word(const u16 __code *p,u8 i) { return p[i]; }
u16 index_reused(const u8 *p,u16 i)
{
    u16 offset=i*2U;
    return (u16)p[offset]^offset;
}
void index_write(u8 *p,u16 i,u8 value) { p[(u16)(i*2U)]=value; }
