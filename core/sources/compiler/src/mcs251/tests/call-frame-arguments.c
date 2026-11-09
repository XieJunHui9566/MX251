#include "call-frame-arguments.h"
u32 args_byte(u8 x, u8 y) NATIVE
{
    take_byte(x, y); barrier(); barrier(); take_byte(y, x); barrier(); barrier(); take_byte(x, y); barrier(); barrier();
    take_byte(y, x); barrier(); barrier(); take_byte(x, y); barrier(); barrier();
    return take_byte(y, x) ^ (u16)(x + y);
}
u32 args_word(u16 x, u16 y) NATIVE
{
    take_word(x, y); barrier(); barrier(); take_word(y, x); barrier(); barrier(); take_word(x, y); barrier(); barrier();
    take_word(y, x); barrier(); barrier(); take_word(x, y); barrier(); barrier();
    return take_word(y, x) ^ (u16)(x + y);
}
u32 args_dword(u32 x, u32 y) NATIVE
{
    take_dword(x, y); barrier(); barrier(); take_dword(y, x); barrier(); barrier(); take_dword(x, y); barrier(); barrier();
    take_dword(y, x); barrier(); barrier(); take_dword(x, y); barrier(); barrier();
    return take_dword(y, x) ^ (x + y);
}
u32 args_native(u32 x, u32 y) NATIVE
{
    take_native(x, y, 1); barrier(); barrier(); take_native(y, x, 2); barrier(); barrier(); take_native(x, y, 3); barrier(); barrier();
    take_native(y, x, 4); barrier(); barrier(); take_native(x, y, 5); barrier(); barrier();
    return take_native(y, x, 6) ^ (x + y);
}
u32 args_pointer(const __xdata u8 *p, u16 y) NATIVE
{
    take_pointer(p, y); barrier(); barrier(); take_pointer(p, y); barrier(); barrier(); take_pointer(p, y); barrier(); barrier();
    take_pointer(p, y); barrier(); barrier(); take_pointer(p, y); barrier(); barrier();
    return take_pointer(p, y) ^ y;
}
