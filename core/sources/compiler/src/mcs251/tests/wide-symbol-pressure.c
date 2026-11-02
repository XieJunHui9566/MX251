typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
typedef union {u32 u;float f;} bits;
#define ABI __reentrant __sdcccall(4)
extern volatile __data u8 g8;
extern volatile __data s8 gs8;
extern volatile __data u16 g16;
extern volatile __data s16 gs16;
extern volatile __data u32 g32;
extern volatile __data float gf;
extern __data u32 plain32;
u32 old_mutate(u32,u32) ABI;
u32 old_return(u32,u32) ABI;

u32 gs_pressure(u32 x,u32 n) ABI {u32 a0,a1,a2,a3,a4,a5,a6,a7,z;g32=x;
a0=g32;g32+=n+0UL;
a1=g32;g32+=n+17UL;
a2=g32;g32+=n+34UL;
a3=g32;g32+=n+51UL;
a4=g32;g32+=n+68UL;
a5=g32;g32+=n+85UL;
a6=g32;g32+=n+102UL;
a7=g32;g32+=n+119UL;
z=old_return(x,n);return z^a0^a1^a2^a3^a4^a5^a6^a7;}
