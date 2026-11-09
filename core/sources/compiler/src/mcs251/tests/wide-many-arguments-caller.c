#include "wide-many-arguments.h"
const u8 __code code_values[4] = {0x12, 0x96, 0xa5, 0xef};
u8 __xdata ram_values[4];
u32 answers[3];
u32 legacy_call(u8 x);
void compute(u8 x)
{
    /* Keep x alive across both calls; both callees consume every parameter. */
    answers[0] = many_mix(MANY_VALUES(x)) ^ x;
    answers[1] = legacy_mix(MANY_VALUES(x)) ^ x;
    answers[2] = legacy_call(x) ^ x;
}
