#include "edata-members.h"

__edata MemberBlock member_block;
volatile __edata MemberBlock member_device;
__edata u8 member_targets[2];

void member_write(u8 seed) NATIVE
{
    member_block.lead = 0xa5;
    member_block.word = (u16)(0xffe0u + seed);
    member_block.dword = 0xfedcba98UL ^ ((u32)seed << 17);
    member_block.pointer = &member_targets[seed & 1];
    member_block.tail = 0x5a;
    member_block.bytes[0] = seed;
    member_block.bytes[4] = (u8)~seed;
}

u32 member_read(void) NATIVE
{
    return member_block.dword ^ ((u32)member_block.word << 8) ^
        member_block.lead ^ ((u32)member_block.tail << 24) ^
        member_block.bytes[0] ^ ((u32)member_block.bytes[4] << 16);
}

u8 *member_pointer(void) NATIVE { return member_block.pointer; }

void member_branch(u8 seed) NATIVE
{
    if (seed & 1) {
        member_block.pointer = &member_targets[1];
        member_block.word = 0x1234;
    } else {
        member_block.pointer = &member_targets[0];
        member_block.word = 0x1234;
    }
}

u32 member_volatile(u8 seed) NATIVE
{
    member_device.lead = 0x96;
    member_device.word = (u16)(0x8123u + seed);
    member_device.dword = 0x76543210UL ^ seed;
    member_device.pointer = &member_targets[seed & 1];
    member_device.tail = 0x69;
    return member_device.dword ^ ((u32)member_device.word << 8) ^
        member_device.lead ^ ((u32)member_device.tail << 24) ^
        *member_device.pointer;
}

u32 member_alias(u8 seed) NATIVE
{
    member_block.word = 0x4321;
    member_mutate(&member_block.word, (u16)(seed * 257u));
    return member_block.word ^ member_block.dword;
}

u32 member_work(u8 seed)
{
    u32 result;
    member_targets[0] = 0x39;
    member_targets[1] = 0xc6;
    member_write(seed);
    result = member_read();
    *member_pointer() ^= seed;
    result ^= member_alias(seed);
    result ^= member_volatile(seed);
    /* The pointer field's neighbours detect a four-byte physical store. */
    result ^= ((u32)member_block.tail << 24) ^ member_block.dword;
    return result ^ member_targets[0] ^ ((u32)member_targets[1] << 16) ^ member_format(seed);
}

u32 member_native(u8 seed) NATIVE
{
    member_targets[0] = (u8)(0x53u + seed);
    member_targets[1] = (u8)(0xacu - seed);
    member_write(seed);
    member_branch(seed);
    return member_read() ^ member_volatile(seed) ^ *member_pointer();
}
