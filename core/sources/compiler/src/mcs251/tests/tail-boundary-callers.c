#include "tail-boundary.h"
u8 tail8(u8 x) OLD { return leaf8(x); }
u16 tail16(u16 x) OLD { return leaf16(x); }
u32 tail32(u32 x) OLD { return leaf32(x); }
float tailf(float x) OLD { return leaff(x); }
u8 __xdata *tailp(u8 __xdata *x) OLD { return leafp(x); }
u32 incoming_stack(u32 x,u32 y) OLD { return leaf32(x ^ y); }
u32 outgoing_stack(u32 x) OLD { return pair32(x,x + 3UL); }
u32 local_address(u32 x) OLD { u32 local = x ^ 0x12345678UL; return read_local(&local); }
u32 parameter_address(u32 x) OLD { return read_local(&x); }
u32 after_call(u32 x) OLD { return leaf32(x) + 1UL; }
u32 converted_result(u8 x) OLD { return leaf8(x); }
u32 native_tail(u32 x,u32 y) NATIVE { return native_pair(y,x); }
u32 mixed_abi(u32 x,u32 y) OLD { return native_pair(x,y); }
void implicit_void(void) OLD { sink0(); }
void discarded_result(u32 x) OLD { leaf32(x); }
void dispatch(u8 key) OLD
{
    switch (key & 7U) {
    case 0: sink0(); break;
    case 1: sink1(); break;
    case 2: sink2(); break;
    case 3: sink3(); break;
    case 4: sink4(); break;
    case 5: sink5(); break;
    case 6: sink6(); break;
    default: sink7(); break;
    }
}
