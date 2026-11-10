#include "machine-board.h"

struct df_record {
    u8 before;
    u16 count;
    u32 total;
    u8 middle;
    u16 last;
    u8 after;
};

extern struct df_record __data df_record;
extern u16 __data df_words[4];
void df_alias(u8 value);
u32 df_step(u16 value);
u32 df_case(u8 seed);
u32 df_volatile(u16 value);
