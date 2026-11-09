#ifndef DATA_INDEX_H
#define DATA_INDEX_H
#include "machine-board.h"
extern u8 __data indexed_bytes[8];
extern u16 __data indexed_words[8];
extern u32 __data indexed_longs[4];
u16 data_index_touch(u8 index,u16 value);
u16 data_index_read(const u16 *p);
u32 data_index_case(u8 seed);
#endif
