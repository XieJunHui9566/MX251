/* Explicit EDATA placement, initialized/zero storage and flat pointers.
 * __sdcc_external_startup poisons RAM before C initialization so a simulator
 * or board runner can distinguish real initialization from power-on zeros. */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

__sfr __at (0x99) SBUF;
volatile __edata u8 buffer[260];
volatile __edata u16 word = 0x1234;
volatile __edata u32 dword = 0x89abcdefUL;
struct pair { u8 first; u16 second; };
volatile __edata struct pair item = {0x56, 0x789a};
volatile __edata u8 initial[3] = {0x12, 0x34, 0x56};
volatile __edata u8 partial[5] = {0x9b};
volatile __edata u8 * __edata saved = &buffer[259];
volatile __edata __at (0x0300) u16 fixed = 0x2468;
volatile __edata __at (0x0310) u8 absolute_uninitialized;
volatile __xdata u8 result;
extern volatile __edata u8 other;
u16 exchange (volatile u8 __edata *p);

_Static_assert (sizeof (u8 __edata *) == 3, "EDATA retains the flat pointer ABI");

static u8 static_value (u8 poison)
{
    static volatile __edata u8 local_zero;
    static volatile __edata u8 local_init = 0x5a;
    static volatile __edata __at(0x0320) u8 local_abs[4] = {0x35};
    u8 passed = local_zero == 0 && local_init == 0x5a;
    passed &= local_abs[0] == 0x35 && local_abs[1] == 0 && local_abs[3] == 0;
    if (poison)
        {
            local_zero = local_init = 0xee;
            local_abs[0] = local_abs[1] = local_abs[2] = local_abs[3] = 0xee;
        }
    return passed;
}

u8 __sdcc_external_startup (void)
{
    u16 i;
    for (i = 0; i < sizeof (buffer); ++i)
        buffer[i] = 0xdd;
    word = 0; dword = 0; item.first = 0; item.second = 0;
    for (i = 0; i < sizeof (initial); ++i)
        initial[i] = 0;
    for (i = 0; i < sizeof (partial); ++i)
        partial[i] = 0xee;
    saved = 0;
    fixed = 0;
    other = 0;
    static_value (1);
    return 0;
}

void main (void)
{
    u16 i;
    u8 passed = word == 0x1234 && dword == 0x89abcdefUL;
    volatile u8 *generic = &buffer[259];
    passed &= item.first == 0x56 && item.second == 0x789a;
    passed &= initial[0] == 0x12 && initial[1] == 0x34 && initial[2] == 0x56;
    passed &= partial[0] == 0x9b && partial[1] == 0 && partial[4] == 0;
    passed &= saved == &buffer[259] && fixed == 0x2468 && other == 0x6b;
    passed &= static_value (0);
    for (i = 0; i < sizeof (buffer); ++i)
        passed &= buffer[i] == 0;
    *saved = 0xa7;
    passed &= *generic == 0xa7 && exchange (saved) == 0xa8;
    absolute_uninitialized = 0x3c;
    passed &= absolute_uninitialized == 0x3c;
    result = passed;
    SBUF = passed ? 'P' : 'F';
    for (;;) {}
}
