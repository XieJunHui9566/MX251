typedef unsigned long u32;
extern u32 helper (u32, u32) __reentrant;
extern u32 (*fp) (u32, u32) __reentrant;
volatile __xdata u32 sink;
u32 direct_call (u32 a, u32 b, u32 c) __reentrant { return helper(a+b,c)+b; }
u32 indirect_call (u32 a, u32 b, u32 c) __reentrant { return fp(a+b,c)+b; }
u32 nested_call (u32 a, u32 b, u32 c) __reentrant { u32 x=helper(a,b);return helper(x,c)+a+b; }
u32 mixed_call (u32 a, u32 b, u32 c) __reentrant { unsigned char x=a; unsigned int y=b; return helper(a+c,b)+x+y+c; }
u32 bank_call (u32 a, u32 b, u32 c) __reentrant __using(1) { return helper(a+b,c)+b; }
void call_isr (void) __interrupt(1) { sink = direct_call(sink,0x12345678UL,0x87654321UL); }
