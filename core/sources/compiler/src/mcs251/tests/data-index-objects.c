#include "data-index.h"
u8 __data indexed_bytes[8];
u16 __data indexed_words[8];
u32 __data indexed_longs[4];

u16 data_index_touch(u8 index,u16 value) {
    indexed_words[index] ^= value;
    return indexed_words[index];
}

u16 data_index_read(const u16 *p) { return *p; }
