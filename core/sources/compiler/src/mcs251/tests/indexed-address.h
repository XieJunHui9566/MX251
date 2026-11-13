#include "machine-board.h"
u8 index_twice(const u8 *,u16);
u8 index_four(const u8 *,u16);
u8 index_code_byte(const u8 __code *,u8);
u16 index_code_word(const u16 __code *,u8);
u16 index_reused(const u8 *,u16);
void index_write(u8 *,u16,u8);
void indexed_init(void);
u32 indexed_case(u8);
