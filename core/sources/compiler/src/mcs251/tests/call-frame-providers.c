#include "call-frame-arguments.h"
volatile __xdata u32 argument_trace;
static u32 record(u32 value) __reentrant
{
    argument_trace = ((argument_trace << 3) | (argument_trace >> 29)) + value;
    return value ^ 0xa5963cc3UL;
}
u32 take_byte(u8 x, u8 y) __reentrant
{
    barrier();
    return record(((u32)x << 8) ^ y);
}
u32 take_word(u16 x, u16 y) __reentrant
{
    barrier();
    return record(((u32)x << 8) ^ y);
}
u32 take_dword(u32 x, u32 y) __reentrant
{
    barrier();
    return record((x << 8) ^ y);
}
u32 take_native(u32 x, u32 y, u8 z) NATIVE
{
    barrier();
    return record((x << 8) ^ y ^ z);
}
u32 take_pointer(const __xdata u8 *p, u16 y) __reentrant
{
    barrier();
    return record(((u32)p[0] << 8) ^ p[1] ^ y);
}
