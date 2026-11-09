#define MANY_NAME legacy_mix
#include "wide-many-arguments-callee.c"
u32 legacy_call(u8 x)
{
    return many_mix(MANY_VALUES(x));
}
