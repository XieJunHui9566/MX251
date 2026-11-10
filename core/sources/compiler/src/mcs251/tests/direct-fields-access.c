#include "direct-fields.h"

/* Offsets 1 and 3 deliberately exercise unaligned word/dword operations. */
u32 df_step(u16 value)
{
    df_record.count += value;
    df_record.total ^= ((u32)df_record.count << 16) | value;
    df_record.last = df_record.count ^ value;
    df_words[1] = df_record.last;
    return df_record.total + df_words[1];
}

static volatile struct df_record __data df_io;

u32 df_volatile(u16 value)
{
    df_io.before = 0x81;
    df_io.middle = 0x63;
    df_io.after = 0xa5;
    df_io.count = value;
    df_io.total = ((u32)value << 16) | (value ^ 0x5197);
    df_io.last = value + 17;
    return df_io.total ^ df_io.count ^ df_io.last ^
           df_io.before ^ df_io.middle ^ df_io.after;
}

u32 df_case(u8 seed)
{
    u8 i;
    u16 value;
    u32 result = 0x13579bdfUL;
    df_record.before = 0xa7;
    df_record.middle = 0x19;
    df_record.after = 0xe3;
    df_record.count = 0xffe0u + seed;
    df_record.total = 0x81527319UL;
    df_words[0] = 0xa519;
    df_words[2] = 0x18e7;
    df_words[3] = 0x7123;
    for (i = 0; i < 8; ++i) {
        value = ((u16)seed << 8) | (seed + i);
        result = (result << 3) ^ result ^ df_step(value);
        df_alias(seed + i);
        result += df_record.count;
        result ^= df_record.total;
        result += df_volatile(value);
    }
    result ^= ((u32)df_record.before << 24) | ((u32)df_record.middle << 16) |
              ((u16)df_record.after << 8);
    return result ^ df_words[0] ^ df_words[2] ^ df_words[3];
}
