#include "shared-tails.h"
volatile u8 __data tail_trim, tail_vtrim, tail_band;
volatile u32 __data tail_frequency;

void tail_pwm(u8 channel, u16 pwm)
{
    if (channel == 0) {
        TREG(1) = (TREG(1) & 0xcd) | ((pwm & 0x300) >> 4) | ((pwm & 0x400) >> 9);
        TREG(0) = (u8)pwm;
    } else if (channel == 1) {
        TREG(5) = (TREG(5) & 0xcd) | ((pwm & 0x300) >> 4) | ((pwm & 0x400) >> 9);
        TREG(4) = (u8)pwm;
    } else if (channel == 2) {
        TREG(9) = (TREG(9) & 0xcd) | ((pwm & 0x300) >> 4) | ((pwm & 0x400) >> 9);
        TREG(8) = (u8)pwm;
    }
}

void tail_clock(u8 mode)
{
    if (mode == 0) {
        TREG(24) = 4;
        tail_trim = TREG(16); tail_vtrim = TREG(17);
        tail_band = (tail_band & 0xfc) | 1;
        TREG(24) = 1; tail_frequency = 24000000UL;
    } else if (mode == 1) {
        TREG(24) = 4;
        tail_trim = TREG(18); tail_vtrim = TREG(19);
        tail_band = (tail_band & 0xfc) | 2;
        TREG(24) = 1; tail_frequency = 32000000UL;
    } else if (mode == 2) {
        TREG(24) = 4;
        tail_trim = TREG(20); tail_vtrim = TREG(21);
        tail_band = (tail_band & 0xfc) | 3;
        TREG(24) = 1; tail_frequency = 40000000UL;
    } else if (mode == 3) {
        TREG(24) = 4;
        tail_trim = TREG(22); tail_vtrim = TREG(23);
        tail_band = (tail_band & 0xfc) | 2;
        TREG(24) = 1; tail_frequency = 22118400UL;
    }
}
