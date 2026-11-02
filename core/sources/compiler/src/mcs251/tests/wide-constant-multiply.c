typedef unsigned long u32;
typedef signed long s32;
#define ABI __reentrant __sdcccall(4)
u32 user_mullong(u32,u32) ABI;
u32 mul13(u32 a,u32 b) ABI {return a*13UL+b;}
u32 mul7(u32 a,u32 b) ABI {return a*7UL+b;}
u32 mul_minus3(u32 a,u32 b) ABI {return a*0xfffffffdUL+b;}
u32 mul_signed(s32 a,u32 b) ABI {return (u32)(a*-3L)+b;}
u32 mul_dense(u32 a,u32 b) ABI {return a*0x12345678UL+b;}
u32 mul_dynamic(u32 a,u32 b) ABI {return a*b;}
u32 user_call(u32 a,u32 b) ABI {return user_mullong(a,13UL)+b;}
u32 mul_live(u32 a,u32 b) ABI {return (a*13UL+b)^(a+b);}
