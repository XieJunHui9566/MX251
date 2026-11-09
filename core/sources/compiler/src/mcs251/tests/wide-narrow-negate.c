#include <stdint.h>
volatile uint16_t input_a,input_b,output;
uint16_t nq(uint16_t a,uint16_t b) { return a/b; }
uint16_t nr(uint16_t a,uint16_t b) { return a%b; }
uint16_t nn(uint16_t a) { return 0u-a; }
uint8_t nb(uint8_t a) { return (uint8_t)(0u-a); }
uint16_t np(uint16_t a,uint16_t b,uint16_t c,uint16_t d) {
 uint16_t keep=a+c, quo=a/b, rem=a%b;
 if(quo>c) return keep+d+rem;
 return (uint16_t)(0u-quo)+keep-d+rem;
}
int16_t signed_q(int16_t a,int16_t b) { return a/b; }
uint32_t long_q(uint32_t a,uint32_t b) { return a/b; }
uint16_t baud_reload(uint32_t baud) {
    return (uint16_t)(0u - (uint16_t)((40000000UL / baud + 2) / 4));
}
uint16_t loop_negate(uint16_t a, uint8_t n) {
    uint16_t sum = 0;
    while (n--) sum += (uint16_t)(0u - a++);
    return sum;
}
