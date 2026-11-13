#include "abi4-backend-choice.h"

u16 negate_word(u16 value) ABI { return (u16)(0u-value); }
u8 negate_byte(u8 value) ABI { return (u8)(0u-value); }

u16 negate_loop(u16 value,u8 count) ABI
{
    u16 sum=0;
    while(count--) sum+=(u16)(0u-value++);
    return sum;
}

u8 byte_loop(u8 value,u8 count) ABI
{
    while(count--) value=(value<<1)|(value>>7);
    return value;
}

u16 mixed_calls(u16 value,u8 count,u16 keep) ABI
{
    while(count--) value=negate_word(ordinary_step(value))+keep;
    return value;
}

u16 divide_negate(u32 divisor) ABI
{
    return (u16)(0u-(u16)((40000000UL/divisor+2)/4));
}
