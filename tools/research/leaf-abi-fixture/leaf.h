typedef unsigned char u8;
typedef unsigned int u16;
extern volatile u8 sampled_a, sampled_b;
u8 leaf_mix(u8 a, u8 b);
void compute(u8 x, u8 y);
extern u8 answers[16];
