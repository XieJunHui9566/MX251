#include "loop-cursors.c"
extern void cursor_observer(void);
volatile u8 __xdata cursor_volatile[32];

void cursor_dynamic(u8 n)
{
    u8 i;
    for (i = 0; i < n; ++i) cursor_x[i] = cursor_a[i];
}

void cursor_call(void)
{
    u8 i;
    for (i = 0; i < 32; ++i) {
        cursor_x[i] = cursor_a[i];
        cursor_observer();
    }
}

void cursor_ordered(void)
{
    u8 i;
    for (i = 0; i < 32; ++i) cursor_volatile[i] = i;
}

void cursor_wrap(void)
{
    u8 i = 0;
    do { cursor_x[i] = i; } while (++i);
}

void cursor_parameter(u8 __xdata *p)
{
    u8 i;
    for (i = 0; i < 32; ++i) p[i] = i;
}
