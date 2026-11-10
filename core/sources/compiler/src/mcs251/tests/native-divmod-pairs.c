#include "native-divmod-pairs.h"
volatile uint16_t pair_sample;

uint16_t pair_qr(uint16_t a,uint16_t b) PAIR_ABI
{ uint16_t q=a/b; return (uint16_t)(q*17u) ^ (a%b); }
uint16_t pair_rq(uint16_t a,uint16_t b) PAIR_ABI
{ uint16_t r=a%b; return (uint16_t)(r*13u) ^ (a/b); }
uint16_t pair_byte(uint8_t a,uint8_t b) PAIR_ABI
{ uint8_t r=a%b,q=a/b; return ((uint16_t)q<<8)|r; }
uint16_t pair_byte_wide(uint8_t a,uint8_t b) PAIR_ABI
{ uint16_t q=a/b,r=a%b; return q+r; }
uint16_t pair_truncated(uint16_t a,uint16_t b) PAIR_ABI
{ uint8_t r=a%b,q=a/b; return ((uint16_t)q<<8)|r; }
uint16_t pair_changed(uint16_t a,uint16_t b) PAIR_ABI
{ uint16_t q=a/b; a+=3u; return q^(a%b); }
uint16_t pair_branch(uint16_t a,uint16_t b) PAIR_ABI
{ uint16_t q=7; if(a&1u)q=a/b; return q^(a%b); }
uint16_t pair_volatile(uint16_t a,uint16_t b) PAIR_ABI
{
    uint16_t q;
    pair_sample=a; q=pair_sample/b;
    pair_sample=a^0xa55au;
    return q^(pair_sample%b);
}
uint16_t pair_call(uint16_t a,uint16_t b) PAIR_ABI
{ uint16_t q=a/b; return q^pair_touch(a)^(a%b); }
uint16_t pair_loop(uint16_t a,uint16_t b) PAIR_ABI
{
    uint16_t total=0;
    uint8_t i;
    for(i=0;i<9;++i) {
        uint16_t r=a%b,q=a/b;
        total=(uint16_t)(total+q*3u+r);
        a=(uint16_t)(a+1031u);
    }
    return total;
}
