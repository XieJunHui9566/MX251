#include "wide-parameter-write.h"
u16 pv_bump(u16 *,u8);
u32 pv_ordinary(u32,u16,const u8 *,u8);
u32 pv_code(u32,const u8 __code *,u8);
u32 pv_signed(u32,s8,u8);
u32 pv_alias(u32,u16,u8);
u32 pv_switch(u32,u16,u8);
u32 pv_case(u8);
void pv_init(void);
