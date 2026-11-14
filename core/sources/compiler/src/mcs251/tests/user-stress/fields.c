#include "stress.h"
typedef struct { u8 tag; u16 count; u32 sum; u8 bytes[8]; } Record;
static __edata Record records[2];

u32 fields_clean(u8 seed)
{
    u8 i;
    records[0].tag = seed;
    records[0].count = (u16)(0xfff0U + seed);
    records[0].sum = 0x12345678UL ^ seed;
    for (i = 0; i < 8; ++i) records[0].bytes[i] = (u8)(seed + 17U * i);
    for (i = 0; i < 8; ++i) {
        records[0].count = (u16)(records[0].count + records[0].bytes[i]);
        records[0].sum ^= (u32)records[0].count << (i & 7);
    }
    return records[0].sum ^ ((u32)records[0].tag << 24) ^ records[0].count;
}

u32 fields_messy(u8 seed)
{
    Record *a = records, *b = &records[0];
    u8 *cursor, n = 0;
    a->tag = seed; b->count = (u16)(0xfff0U + seed);
    a->sum = 0x12345678UL ^ seed; cursor = b->bytes;
fill:
    *(cursor + n) = (u8)(seed + 17U * n);
    if (++n != 8) goto fill;
    n = 0;
work:
    if (n & 1) a->count = (u16)(b->count + *(cursor + n));
    else b->count = (u16)(a->count + cursor[n]);
    a->sum = b->sum ^ ((u32)b->count << (n & 7));
    if (++n < 8) goto work;
    return b->sum ^ ((u32)a->tag << 24) ^ a->count;
}
