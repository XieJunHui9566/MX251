#include "machine-board.h"
u32 transaction_trace;
void transaction(u8 address, u8 value)
{
    transaction_trace = (transaction_trace << 5) ^ transaction_trace ^
                        ((u16)address << 8) ^ value;
}
