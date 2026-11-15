#include <stdint.h>
#include <stdio.h>
#ifndef DECIMAL_ABI
#define DECIMAL_ABI
#endif

/* The legacy candidate is generated even when the wide backend wins.
 * Word DIV must not be interpreted using unrelated known A/B bytes. */
unsigned char decimal32(uint32_t value) DECIMAL_ABI
{
    unsigned char digits[11], n=0, bit, total;
    do {
        uint16_t part=0;
        bit=4;
        do {
            part=(uint16_t)((part<<8)|(unsigned char)(value>>24));
            value=(uint32_t)(value<<8);
            value|=(unsigned char)(part/10u);
            part%=10u;
        } while(--bit);
        digits[n++]=(unsigned char)('0'+part);
    } while(value);
    total=n;
    do { putchar(digits[--n]); } while(n);
    return total;
}
