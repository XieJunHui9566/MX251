#include "edata-members.h"
#include <stdio.h>
u32 member_format(u8 seed)
{
    char buffer[24];
    u8 i;
    u32 result = 0;
    sprintf(buffer, "ID:0x%x", (unsigned int)(member_format_bias + seed));
    for (i = 0; buffer[i]; ++i) result = (result << 3) ^ (u8)buffer[i];
    sprintf(buffer, "read color:0x%04x", (unsigned int)(seed * 257u));
    for (i = 0; buffer[i]; ++i) result = (result << 3) ^ (u8)buffer[i];
    return result;
}
