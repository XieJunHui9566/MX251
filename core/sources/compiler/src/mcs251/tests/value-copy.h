#include "machine-board.h"
u32 copy_sink0(u16,u16,u8,u8,u32);
u32 copy_sink1(u16,u16,u8,u8,u32);
u32 copy_sink2(u16,u16,u8,u8,u32);
u32 copy_sink3(u16,u16,u8,u8,u32);
u32 copy_fanout(u8,u16,u16,u8,u32,const u8 *);
u16 copy_mutating(u16,u8);
u16 copy_snapshot(volatile u16 *,u8);
u16 copy_pointer(const u8 *,u8);
u32 copy_case(u8);
void copy_init(void);
