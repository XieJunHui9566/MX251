#include "edata-members.h"
void member_mutate(u16 *p, u16 x) NATIVE
{
    *p ^= x;
    member_block.dword += 0x01020304UL;
}
