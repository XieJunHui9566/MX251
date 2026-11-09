/* AI8051U-34K64: existing 40 MHz / UART1 P3.0/P3.1 setup. Timer0
 * mode 1 at 12T measures the renderer plus deterministic trace callbacks. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
void render_a(u16, u16, u16, u16, u8 *, u8);
void render_b(u16, u16, u16, u16, u8 *, u8);
void render_c(u16, u16, u16, u16, u8 *, u8);
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
__sfr __at(0x89) TMOD;
__sfr __at(0x8a) TL0;
__sfr __at(0x8c) TH0;
__sbit __at(0xaf) EA;
__sbit __at(0x99) TI;
__sbit __at(0x8c) TR0;
__sbit __at(0x8d) TF0;
u16 POINT_COLOR;
struct screen { u16 width, height; } screen;
static u32 trace;
static u16 count;
static u8 nested, checksum;

static void record(u16 value)
{
    trace = (trace << 5) ^ trace ^ value;
    ++count;
}
void window(u16 x, u16 y, u16 right, u16 bottom)
{
    u8 key[2];
    record(1); record(x); record(y); record(right); record(bottom);
    if (nested) {
        nested = 0;
        key[0] = 0x83; key[1] = 0x93;
        render_b(123, 234, 0x5aa5, 0xaa55, key, 1);
    }
}
void pixel(u16 color) { record(2); record(color); }
void point(u16 x, u16 y) { record(3); record(x); record(y); record(POINT_COLOR); }
static void send(u8 value)
{
    checksum ^= value;
    SBUF = value;
    while (!TI) {}
    TI = 0;
}
void main(void)
{
    u8 input, which, mode, slot, entries, key[2], overflow, unique;
    u16 x, y, fc, bc, ticks;
    EA = 0;
    P3 |= 3; P3M1 &= 0xfc; P3M0 = (P3M0 & 0xfc) | 2;
    P_SW1 &= 0x3f; AUXR &= 0x67; SCON = 0x40;
    T2L = 0xa9; T2H = 0xff; AUXR |= 0x15;
    WTST = 0; CKCON = 0;
    TMOD = 1; AUXR &= 0x7f; TR0 = 0;
    screen.width = 320; screen.height = 240;
    unique = render_a != render_b && render_a != render_c && render_b != render_c;
    for (;;) for (input = 0; input < 96; ++input) {
        which = input % 3; mode = (input / 3) & 1; slot = (input / 6) & 3;
        entries = which == 0 ? 58 : which == 1 ? 8 : 4;
        slot = slot == 0 ? 0xff : slot == 1 ? 0 : slot == 2 ? entries / 2 : entries - 1;
        key[0] = 0x80 + slot; key[1] = 0x90 + slot;
        x = 0xfff0 + input; y = 0xffe0 + input;
        fc = 0x5123 ^ input; bc = 0xcba9 + input;
        trace = 0x12345678; count = 0; POINT_COLOR = 0x7654; nested = input >= 48;
        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        if (which == 0) render_a(x, y, fc, bc, key, mode);
        else if (which == 1) render_b(x, y, fc, bc, key, mode);
        else render_c(x, y, fc, bc, key, mode);
        TR0 = 0; ticks = ((u16)TH0 << 8) | TL0; overflow = TF0;
        checksum = 0;
        send('F'); send('S'); send('H'); send('1'); send(input);
        send(trace >> 24); send(trace >> 16); send(trace >> 8); send(trace);
        send(count >> 8); send(count); send(POINT_COLOR >> 8); send(POINT_COLOR);
        send(ticks >> 8); send(ticks); send(overflow); send(unique);
        send(checksum); send(13); send(10);
    }
}
