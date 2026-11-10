#include "machine-board.h"

struct request { u8 type, code; u16 value, index, length; };
struct request __data request;
u8 __data device, input_flags, output_flags;
extern u32 transaction_trace;
void transaction(u8 address, u8 value);

void apply_feature(void)
{
    u8 endpoint;
    if (device != 4 || request.type != 2 || request.value ||
        request.length || (request.index >> 8)) {
        transaction(0, 0xff);
        return;
    }
    endpoint = request.index & 15;
    switch (request.index) {
    case 0x81:
    case 0x82:
        input_flags |= 1 << endpoint;
        transaction(14, endpoint);
        transaction(17, 16);
        transaction(14, 0);
        break;
    case 1:
        output_flags |= 1 << endpoint;
        transaction(14, endpoint);
        transaction(20, 32);
        transaction(14, 0);
        break;
    default:
        transaction(0, 0xff);
        return;
    }
    transaction(0, 0);
}

u16 swap_pair(u16 value) { return (value << 8) | (value >> 8); }

u32 body_case(u8 seed)
{
    u8 i, which;
    u32 result = 0x13579bdfUL;
    transaction_trace = 0x1972513aUL;
    input_flags = seed ^ 0xa5;
    output_flags = seed ^ 0x5a;
    for (i = 0; i < 8; ++i) {
        which = (seed + i) & 3;
        request.type = ((seed + i) & 8) ? 2 : 0x82;
        request.value = ((seed + i) & 16) ? 0 : 0x100;
        request.length = 0;
        request.index = which == 0 ? 1 : which == 1 ? 0x81 : which == 2 ? 0x82 : 0x83;
        device = ((seed + i) & 32) ? 4 : 3;
        apply_feature();
        result = (result << 3) ^ result ^ transaction_trace;
        result += swap_pair(((u16)input_flags << 8) | output_flags);
    }
    return result;
}

/* C cycles retain the path-based selector, including an unstructured loop. */
u16 body_loop(u8 count) { u16 value=0; while(count--) value+=count; return value; }
u16 body_goto(u8 x, u8 n) {
    if(x) goto right;
left:
    x += 3;
    if(!n--) return x;
right:
    x ^= 9;
    if(n--) goto left;
    return x;
}
