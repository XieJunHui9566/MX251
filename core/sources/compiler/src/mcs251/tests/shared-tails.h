typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#define TREG(n) (*(volatile u8 __xdata *)(0x2000UL + (n)))
extern volatile u8 __data tail_trim, tail_vtrim, tail_band;
extern volatile u32 __data tail_frequency;
void tail_pwm(u8 channel, u16 pwm);
void tail_clock(u8 mode);
u16 tail_result(u8 kind, u16 input);
u16 tail_dependencies(u8 kind, u16 input);
u32 tail_case(u8 kind, u8 seed);
