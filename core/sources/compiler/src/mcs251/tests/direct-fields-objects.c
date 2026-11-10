#include "direct-fields.h"

struct df_record __data df_record;
u16 __data df_words[4];

void df_alias(u8 value)
{
    u8 *bytes = (u8 *)&df_record;
    /* Independent byte accesses observe the big-endian native field stores
     * and modify part of each field before the caller reads it again. */
    bytes[2] ^= value;
    bytes[4] += value;
    bytes[6] ^= 0x91;
}
