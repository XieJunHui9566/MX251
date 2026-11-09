/* AI8051U-34K64 at 40 MHz; UART1 P3.0/P3.1, 115200 8N1.
 * Emits results for an independent host oracle; contains no target-side oracle.
 * Compile with call-frame-arguments.c, call-frame-providers.c and machine-call-clobber.asm. */
#include "call-frame-arguments.h"
__sfr __at(0xb0) P3;
__sfr __at(0xb1) P3M1;
__sfr __at(0xb2) P3M0;
__sfr __at(0xa2) P_SW1;
__sfr __at(0x8e) AUXR;
__sfr __at(0x98) SCON;
__sfr __at(0x99) SBUF;
__sfr __at(0xd6) T2H;
__sfr __at(0xd7) T2L;
__sfr __at(0xe9) WTST;
__sfr __at(0xea) CKCON;
__sbit __at(0xaf) EA;
__sbit __at(0x99) TI;
static const __code u32 inputs[] = {
    0, 1, 2, 3, 7, 0x80, 0x100, 0xff, 0xffff, 0x10000,
    0x7fffffffUL, 0x80000000UL, 0xaaaa5555UL, 0x5555aaaaUL,
    0xdeadbeefUL, 0xffffffffUL
};
static __xdata u32 results[16][10];
static __xdata u8 pointer_bytes[2];
static u8 checksum;
static void send(u8 v)
{
    u16 limit = 65535U;
    checksum ^= v;
    SBUF = v;
    while (!TI) if (--limit == 0) for (;;) {}
    TI = 0;
}
static void word(u32 v)
{
    send((u8)(v >> 24)); send((u8)(v >> 16));
    send((u8)(v >> 8)); send((u8)v);
}
void main(void)
{
    u8 i, j;
    u32 x, y;
    EA = 0;
    P3 |= 3; P3M1 &= 0xfc; P3M0 = (P3M0 & 0xfc) | 2;
    P_SW1 &= 0x3f; AUXR &= 0x67; SCON = 0x40;
    T2L = 0xa9; T2H = 0xff; AUXR |= 0x15;
    WTST = 0; CKCON = 0;
    for (i = 0; i < 16; ++i) {
        x = inputs[i]; y = (x >> 7) ^ 0x96a55a69UL;
        argument_trace = 0;
        results[i][0] = args_byte((u8)x, (u8)y);
        results[i][1] = argument_trace;
        argument_trace = 0;
        results[i][2] = args_word((u16)x, (u16)y);
        results[i][3] = argument_trace;
        argument_trace = 0;
        results[i][4] = args_dword(x, y);
        results[i][5] = argument_trace;
        argument_trace = 0;
        results[i][6] = args_native(x, y);
        results[i][7] = argument_trace;
        pointer_bytes[0] = (u8)x; pointer_bytes[1] = (u8)y;
        argument_trace = 0;
        results[i][8] = args_pointer(pointer_bytes, (u16)y);
        results[i][9] = argument_trace;
    }
    for (;;) for (i = 0; i < 16; ++i) {
        checksum = 0;
        send('A'); send('R'); send('G'); send('1'); send(i);
        for (j = 0; j < 10; ++j) word(results[i][j]);
        send(checksum); send(13); send(10);
    }
}
