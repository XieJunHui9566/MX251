#ifndef MACHINE_CALL_PLAN_H
#define MACHINE_CALL_PLAN_H
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifndef NATIVE
#define NATIVE __reentrant __sdcccall(4)
#endif
void barrier(void) __reentrant;
u16 repeated_word(u16 x,u16 y) NATIVE;
u32 repeated_dword(u32 x,u32 y) NATIVE;
u32 branches(u32 x,u32 y) NATIVE;
u16 loop_values(u16 x,u16 y) NATIVE;
u16 redefine(u16 x,u16 y) NATIVE;
#endif
