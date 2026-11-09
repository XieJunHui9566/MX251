#include "wide-parameter-write.h"
u32 pa_ordinary(u32,u16,u16,u8);
u32 pa_native(u32,u32,u8,u16,u32,u8 PTR *,u8 PTR *) ABI;
u32 pa_alias(u32,u16,u8);
u32 pa_partial(u32,u32,u8);
u32 pa_volatile(u32,u16,u8);
void pa_touch(u16 *,u8);
u32 pa_case(u8);
