#include "tail-boundary.h"
volatile u32 tail_trace;
u8 leaf8(u8 x) OLD { return x ^ 0xa5U; }
u16 leaf16(u16 x) OLD { return x + 0x8123U; }
u32 leaf32(u32 x) OLD { return (x ^ 0x89abcdefUL) + 0x1234567UL; }
float leaff(float x) OLD { return x; }
u8 __xdata *leafp(u8 __xdata *x) OLD { return x; }
u32 pair32(u32 x,u32 y) OLD { return x + y; }
u32 read_local(u32 __xdata *x) OLD { return *x ^ 0x56789abcUL; }
u32 native_pair(u32 x,u32 y) NATIVE { return (x << 3) ^ y; }
void sink0(void) OLD { tail_trace = 0x13579b00UL; }
void sink1(void) OLD { tail_trace = 0x13579b01UL; }
void sink2(void) OLD { tail_trace = 0x13579b02UL; }
void sink3(void) OLD { tail_trace = 0x13579b03UL; }
void sink4(void) OLD { tail_trace = 0x13579b04UL; }
void sink5(void) OLD { tail_trace = 0x13579b05UL; }
void sink6(void) OLD { tail_trace = 0x13579b06UL; }
void sink7(void) OLD { tail_trace = 0x13579b07UL; }
u8 context_echo(u8 x) OLD { return x ^ 0xa5U; }
