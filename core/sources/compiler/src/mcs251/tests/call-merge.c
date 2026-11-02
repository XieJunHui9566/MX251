typedef unsigned long u32;
extern void emit(unsigned int value);
volatile __xdata unsigned char observed;
u32 adjacent(u32 keep) __reentrant {u32 live=keep+observed;emit(8);emit(32);emit(8);return live;}
u32 observable_boundary(u32 keep) __reentrant {u32 live=keep+observed;emit(8);++observed;emit(32);return live;}
u32 branch_boundary(u32 keep) __reentrant {u32 live=keep+observed;emit(8);if(observed)emit(32);return live;}
u32 inline_boundary(u32 keep) __reentrant {u32 live=keep+observed;emit(8);__asm nop __endasm;emit(32);return live;}
u32 bank_boundary(u32 keep) __reentrant __using(1) {u32 live=keep+observed;emit(8);emit(32);return live;}
u32 used_argument(u32 keep) __reentrant {u32 live=keep+observed;emit(8);emit((unsigned int)live);return live;}
