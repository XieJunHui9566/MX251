#include "stress.h"
typedef u32 (*Operation)(u32, u8);
u32 callback_clean(u8 seed)
{
    u8 i; u32 result = 0xabcdef01UL ^ seed; Operation fn;
    for (i = 0; i < 24; ++i) {
        fn = (i & 1) ? callback_add : callback_xor;
        result = fn(result, (u8)(seed + i));
    }
    return result;
}
u32 callback_messy(u8 seed)
{
    u8 i = 0; u32 result = 0xabcdef01UL ^ seed;
    Operation fn, aliases[2]; aliases[0] = callback_xor; aliases[1] = callback_add;
again:
    fn = *(aliases + (i & 1));
    if (i & 2) result = (*fn)(result, (u8)(seed + i));
    else result = fn(result, (u8)(seed + i));
    if (++i < 24) goto again;
    return result;
}
