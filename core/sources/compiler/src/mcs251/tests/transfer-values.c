/* Register-copy elimination integration inputs. GPL-2.0-or-later. */
typedef unsigned int u16;
typedef unsigned long u32;
extern volatile u32 shared_value;
extern u32 external_value(u32);
u32 chain32(u32 a,u32 b,u32 c) __reentrant { a+=b; a+=c; a<<=1; return a+b; }
u32 repeated(u32 a,u32 b,u32 c) __reentrant { a+=b; a<<=1; a+=b; a<<=2; a+=b; return a+c; }
u16 chain16(u16 a,u16 b,u16 c) __reentrant { a+=b; a+=c; a<<=2; return a+b; }
u32 calls(u32 a,u32 b) __reentrant { a+=b; a=external_value(a); return a+b; }
u32 observable(u32 a,u32 b) __reentrant { shared_value=a+b; return shared_value+b; }
u32 branch_join(u32 a,u32 b) __reentrant { if(a>b)a+=b; else a-=b; return (a<<1)+b; }
u32 inline_boundary(u32 a,u32 b,u32 c) __reentrant { a+=b; __asm nop __endasm; a+=c; return (a<<1)+b; }
void ordinary_isr(void) __interrupt(5) { u32 a=shared_value; a+=3; a<<=1; shared_value=a; }
