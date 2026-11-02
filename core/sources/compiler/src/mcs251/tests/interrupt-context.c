/* 中断现场回归：叶函数、跨模块调用、独立寄存器组和用户裸入口。 */
volatile unsigned long __xdata irq_values[4];
unsigned long irq_external(unsigned long value);

void irq_leaf(void) __interrupt(1)
{
    irq_values[0]++;
}

void irq_calls(void) __interrupt(3)
{
    volatile unsigned char guard[19];
    unsigned char i;
    for (i = 0; i < 19; ++i)
        guard[i] = i;
    irq_values[1] = irq_external(irq_values[0]);
    for (i = 0; i < 19; ++i)
        if (guard[i] != i)
            irq_values[2]++;
}

void irq_bank(void) __interrupt(4) __using(1)
{
    irq_values[3]++;
}

void irq_naked(void) __interrupt(0) __naked
{
    __asm
    reti
    __endasm;
}
