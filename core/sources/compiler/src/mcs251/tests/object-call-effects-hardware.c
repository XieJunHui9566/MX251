/* AI8051U-34K64 at 40 MHz; UART1 P3.0/P3.1, 115200 8N1.
 * Emits results for an independent host oracle; contains no target-side oracle.
 * Compile alongside object-call-effects-store.c and object-call-effects-user.c. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
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
extern volatile __data struct { u8 tag; u8 bytes[3]; u32 word; } state;
extern volatile __bit ready;
u32 live_objects(u32, u32) __reentrant;
u32 live_registers(u32, u32) __reentrant;
static const __code u32 inputs[] = {
    0, 1, 2, 3, 7, 0x80, 0x100, 0xff, 0xffff, 0x10000,
    0x7fffffffUL, 0x80000000UL, 0xaaaa5555UL, 0x5555aaaaUL,
    0xdeadbeefUL, 0xffffffffUL
};
static __xdata u32 results[16][3];
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
        results[i][0] = live_objects(x, y);
        results[i][1] = live_registers(x, y);
        results[i][2] = ((u32)state.tag << 24) | ((u32)state.bytes[0] << 16)
            | ((u32)state.bytes[1] << 8) | state.bytes[2];
    }
    for (;;) for (i = 0; i < 16; ++i) {
        checksum = 0;
        send('O'); send('B'); send('J'); send('1'); send(i);
        for (j = 0; j < 3; ++j) word(results[i][j]);
        send(checksum); send(13); send(10);
    }
}
