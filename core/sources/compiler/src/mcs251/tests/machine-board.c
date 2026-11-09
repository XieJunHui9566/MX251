#include "machine-board.h"
#ifdef __SDCC
#define SFR(name,address) __sfr __at(address) name
#define SBIT(name,address) __sbit __at(address) name
#else
#define SFR(name,address) sfr name = address
#define SBIT(name,address) sbit name = address
#endif
SFR(P3,0xb0); SFR(P3M1,0xb1); SFR(P3M0,0xb2); SFR(P_SW1,0xa2);
SFR(AUXR,0x8e); SFR(SCON,0x98); SFR(SBUF,0x99);
SFR(T2H,0xd6); SFR(T2L,0xd7); SFR(WTST,0xe9); SFR(CKCON,0xea);
SFR(TMOD,0x89); SFR(TL0,0x8a); SFR(TH0,0x8c);
SBIT(EA,0xaf); SBIT(TI,0x99); SBIT(TR0,0x8c); SBIT(TF0,0x8d);
static u8 checksum;
static void send(u8 value)
{
    checksum ^= value; SBUF = value;
    while (!TI) {}
    TI = 0;
}
void main(void)
{
    u8 input, kind, overflow;
    u16 ticks, x0, y0, x1, y1, x2, y2;
    u32 result;
    EA = 0;
    P3 |= 3; P3M1 &= 0xfc; P3M0 = (P3M0 & 0xfc) | 2;
    P_SW1 &= 0x3f; AUXR &= 0x67; SCON = 0x40;
    T2L = 0xa9; T2H = 0xff; AUXR |= 0x15;
    WTST = 0; CKCON = 0;
    TMOD = 1; AUXR &= 0x7f; TR0 = 0;
    for (;;) for (kind = 0; kind < 2; ++kind) for (input = 0; input < 64; ++input) {
        x0 = 10 + input; x1 = 40 + (input & 7); x2 = 20 + (input & 15);
        y0 = 2 + (input & 7); y1 = 3 + ((input >> 1) & 7); y2 = 4 + ((input >> 2) & 7);
        POINT_COLOR = 0x5a00 + input; trace = 0x12345678UL;
        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        if (kind == 0) {
            Fill_Triangel(x0,y0,x1,y1,x2,y2);
            result = trace;
        } else result = control(input);
        TR0 = 0; ticks = ((u16)TH0 << 8) | TL0; overflow = TF0;
        checksum = 0;
        send('M'); send('I'); send('R'); send('1'); send(kind); send(input);
        send(result >> 24); send(result >> 16); send(result >> 8); send(result);
        send(ticks >> 8); send(ticks); send(overflow); send(checksum); send(13); send(10);
    }
}
