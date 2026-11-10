typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
volatile __edata u8 state_edata[16];
volatile __xdata u8 state_xdata[16];
#define CLKDIV (*(volatile __xdata u8 *)0x7efe01UL)
u32 state_mix(u32);

/* EAXFR leaves compiler objects and the stack accessible.
 * The live scalars, volatile frame, call and XFR reads straddle each switch. */
u32 state_work(u8 seed, u8 mode)
{
    u32 a = 0x12345678UL + seed, b = 0x87654321UL ^ seed;
    u32 c = 0x13579bdfUL, d = 0x2468ace0UL ^ mode;
    volatile u16 slots[3];
    u8 i, clock;
    __asm setb 0xba.7 __endasm;
    clock = CLKDIV;
    for (i = 0; i < 8; ++i) {
        slots[0] = (u16)a; slots[1] = (u16)b; slots[2] = seed + i;
        state_edata[i] = seed + i;
        state_xdata[i] = seed ^ (i * 7);
        if (mode) { __asm
            clr 0xba.7
            nop
        __endasm; }
        else { __asm clr 0xba.7 __endasm; }
        b += state_edata[i] + slots[1];
        c ^= ((u32)state_xdata[i] << 16) | slots[0];
        a = state_mix(a + slots[2]);
        d = (d << 3) ^ (d >> 5) ^ b;
        if (mode) { __asm
            nop
            setb 0xba.7
        __endasm; }
        else { __asm setb 0xba.7 __endasm; }
        if (CLKDIV != clock) return 0xbad00000UL | i;
        a ^= c + d;
    }
    return a ^ b ^ c ^ d;
}
