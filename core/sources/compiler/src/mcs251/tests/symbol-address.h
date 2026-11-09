#ifndef SYMBOL_ADDRESS_H
#define SYMBOL_ADDRESS_H
#include "machine-board.h"
u16 address_first(const u8 *p,u8 count,u16 salt);
u16 address_stack(u16 salt,const u8 *p,u8 count,const u8 *q);
u16 address_code(const u8 __code *p,u8 count,u16 salt);
u32 address_case(u8 seed);
#endif
