#include <stdint.h>
extern uint32_t quantize_route(float x, unsigned char mode);
volatile uint32_t sample_bits=0x4b765432UL;
volatile unsigned char mode_input;
volatile uint32_t quantized;
void main(void)
{
    union { uint32_t bits; float value; } sample;
    while (1) {
        sample.bits=sample_bits;
        quantized=quantize_route(sample.value,mode_input);
    }
}
