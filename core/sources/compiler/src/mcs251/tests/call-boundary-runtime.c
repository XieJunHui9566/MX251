#include "tail-boundary.h"
static u32 fold(u32 hash,u32 value) { return (hash << 5) ^ hash ^ value; }
u32 tail_check(u8 seed)
{
    union { u32 bits; float value; } input,output;
    u32 x = 0xf1234567UL ^ ((u32)seed * 0x10001UL);
    u32 y = 0x13579bdfUL ^ ((u32)seed * 257UL);
    u32 result = 0x13579bdfUL;
    input.bits = x; output.value = tailf(input.value);
    result = fold(result,tail8((u8)x));
    result = fold(result,tail16((u16)x));
    result = fold(result,tail32(x));
    result = fold(result,output.bits);
    result = fold(result,(u32)tailp((u8 __xdata *)(0x10000UL + (u32)seed * 17UL)));
    result = fold(result,incoming_stack(x,y));
    result = fold(result,outgoing_stack(x));
    result = fold(result,local_address(x));
    result = fold(result,parameter_address(x));
    result = fold(result,after_call(x));
    result = fold(result,converted_result((u8)x));
    result = fold(result,native_tail(x,y));
    result = fold(result,mixed_abi(x,y));
    implicit_void(); result = fold(result,tail_trace);
    dispatch(seed); result = fold(result,tail_trace);
    discarded_result(x); result = fold(result,tail_trace);
    return result;
}
