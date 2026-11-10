#include "shared-tails-callees.c"
#include "shared-tails-values.c"
extern void tail_observer(void);

void tail_cyclic(u8 mode)
{
    u8 i;
    for (i = 0; i < 8; ++i) {
        if (mode) {
            TREG(1) = (TREG(1) & 0xcd) | i;
            TREG(0) = i;
        } else {
            TREG(5) = (TREG(5) & 0xcd) | i;
            TREG(4) = i;
        }
    }
}

void tail_call(u8 mode)
{
    if (mode) {
        TREG(1) = TREG(1) ^ 0x55;
        tail_observer();
        TREG(0) = 1;
    } else {
        TREG(5) = TREG(5) ^ 0xaa;
        tail_observer();
        TREG(4) = 2;
    }
}
