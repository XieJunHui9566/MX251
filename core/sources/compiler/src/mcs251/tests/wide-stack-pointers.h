typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef unsigned long u32;
#ifdef __C251__
#define X far
#define ABI reentrant
#else
#define X __xdata
#define ABI __reentrant __sdcccall(4)
#endif
u32 ptr3(u32,u32,u8 X *) ABI;
u32 ptr8(u32,u32,u8,u8 X *,u16,u32 X *,s8,u8 X *) ABI;
u32 many_ptr(u8 X *,u8 X *,u8 X *,u8 X *,u8 X *,u8 X *,u8 X *,u8 X *) ABI;
u8 X *retptr(u32,u32,u8 X *,u16) ABI;
u32 narrowptr(u32,u32,u8 X *) ABI;
u32 recurseptr(u32,u32,u8 X *,u8 X *) ABI;
u32 change3(u32,u32,u8 X *,u16,u32) ABI;
u32 generic3(u32,u32,u8 *,u16) ABI;
u32 volatile3(u32,u32,volatile u8 X *,u32) ABI;
