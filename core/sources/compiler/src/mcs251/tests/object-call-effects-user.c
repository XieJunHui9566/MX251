typedef unsigned char u8;
typedef unsigned long u32;
extern volatile __data struct { u8 tag; u8 bytes[3]; u32 word; } state;
extern volatile __bit ready;
void store_state(u8) __reentrant;
static volatile __data u8 local_byte;

void chain_store(u8 x) __reentrant
{
    store_state(x);
    state.bytes[1] = x ^ 0x5a;
    local_byte = x;
}

u32 live_objects(u32 x, u32 y) __reentrant
{
    u32 a = x ^ 0x12345678UL;
    u32 b = y + 0x3456789aUL;
    u32 c = x + y;
    chain_store((u8)x);
    return (a + b) ^ c ^ state.word ^ state.bytes[1] ^ (u8)ready;
}

u32 live_registers(u32 x, u32 y) __reentrant
{
    u32 a = x ^ 0x12345678UL;
    u32 b = y + 0x3456789aUL;
    u32 c = x + y;
    chain_store((u8)x);
    return (a + b) ^ c;
}
