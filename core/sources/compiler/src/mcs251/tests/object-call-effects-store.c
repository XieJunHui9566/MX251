typedef unsigned char u8;
typedef unsigned long u32;
volatile __data struct { u8 tag; u8 bytes[3]; u32 word; } state;
volatile __bit ready;
static volatile __data u8 local_byte;
__data __at(0x70) volatile u8 absolute_byte;

void store_state(u8 x) __reentrant
{
    state.tag = x;
    state.bytes[0] = x + 1;
    state.bytes[2] = x ^ 0xa5;
    state.word = 0x87654321UL;
    local_byte = x + 3;
    ready = x & 1;
}

void store_absolute(u8 x) __reentrant { absolute_byte = x; }
