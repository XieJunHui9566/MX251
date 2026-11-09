#ifndef CALL_FRAME_ARGUMENTS_H
#define CALL_FRAME_ARGUMENTS_H
#include "machine-call-plan.h"
extern volatile __xdata u32 argument_trace;
u32 take_byte(u8 x, u8 y) __reentrant;
u32 take_word(u16 x, u16 y) __reentrant;
u32 take_dword(u32 x, u32 y) __reentrant;
u32 take_native(u32 x, u32 y, u8 z) NATIVE;
u32 take_pointer(const __xdata u8 *p, u16 y) __reentrant;
u32 args_byte(u8 x, u8 y) NATIVE;
u32 args_word(u16 x, u16 y) NATIVE;
u32 args_dword(u32 x, u32 y) NATIVE;
u32 args_native(u32 x, u32 y) NATIVE;
u32 args_pointer(const __xdata u8 *p, u16 y) NATIVE;
#endif
