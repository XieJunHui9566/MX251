#include <stdint.h>
static long offset_count(float x)
{
    return (long)(x+0.625f);
}
uint32_t quantize_route(float x, unsigned char mode)
{
    if (mode&1) return (unsigned long)x;
    return (uint32_t)offset_count(x);
}
