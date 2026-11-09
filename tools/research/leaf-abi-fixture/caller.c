#include "leaf.h"
u8 answers[16];
/* Separate translation units, live x/y across every call, byte return,
 * and observable argument stores exercise the automatic ABI contract. */
#define SAMPLE(k) answers[k] = leaf_mix((u8)(x + k), (u8)(y - 7 * k)) ^ sampled_a ^ sampled_b
void compute(u8 x, u8 y)
{
    SAMPLE(0);  SAMPLE(1);  SAMPLE(2);  SAMPLE(3);
    SAMPLE(4);  SAMPLE(5);  SAMPLE(6);  SAMPLE(7);
    SAMPLE(8);  SAMPLE(9);  SAMPLE(10); SAMPLE(11);
    SAMPLE(12); SAMPLE(13); SAMPLE(14); SAMPLE(15);
}
