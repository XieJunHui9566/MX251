/* Existing board setup: AI8051U-34K64, 40 MHz, UART1 P3.0/P3.1 115200. */
#include "wide-many-arguments.h"
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
static u8 checksum;
static void send(u8 value)
{
    u16 limit = 65535U;
    checksum ^= value;
    SBUF = value;
    while (!TI) if (--limit == 0) for (;;) {}
    TI = 0;
}
void main(void)
{
    u16 input;
    u8 i, k;
    EA = 0;
    P3 |= 3; P3M1 &= 0xfc; P3M0 = (P3M0 & 0xfc) | 2;
    P_SW1 &= 0x3f; AUXR &= 0x67; SCON = 0x40;
    T2L = 0xa9; T2H = 0xff; AUXR |= 0x15;
    WTST = 0; CKCON = 0;
    ram_values[0] = 0x33; ram_values[1] = 0x7f;
    ram_values[2] = 0xc0; ram_values[3] = 0x5a;
    for (;;) for (input = 0; input < 256; ++input) {
        compute((u8)input);
        checksum = 0;
        send('M'); send('1'); send('6'); send('1'); send((u8)input);
        for (i = 0; i < 3; ++i)
            for (k = 0; k < 4; ++k) send((u8)(answers[i] >> (24 - 8 * k)));
        send(checksum); send(13); send(10);
    }
}
