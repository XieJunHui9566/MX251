typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
#define XP __xdata
u32 load32(u32 XP *,u32) ABI;u32 alias32(u32 XP *,u32 XP *) ABI;u32 swap32(u32 XP *,u32 XP *) ABI;
u8 XP *advance(u8 XP *,u16) ABI;u8 XP *old_pointer(u8 XP *,u32) ABI;
u32 change(u32 XP *,u32) ABI;u32 poisoned(u32,u32) ABI;
extern u8 XP arena[64];
u32 read32(u32 a,u32 b) ABI {return *(u32 XP *)a^b;}
u32 read16(u32 a,u32 b) ABI {return *(u16 XP *)a^b;}
u32 read8signed(u32 a,u32 b) ABI {return (u32)(signed long)*(s8 XP *)a^b;}
u32 write32(u32 a,u32 b) ABI {*(u32 XP *)a=b;return *(u32 XP *)a;}
u32 write16(u32 a,u32 b) ABI {*(u16 XP *)a=(u16)b;return *(u16 XP *)a;}
u32 write8(u32 a,u32 b) ABI {*(u8 XP *)a=(u8)b;return *(u8 XP *)a;}
u32 volatile32(u32 a,u32 b) ABI {volatile u32 XP *p=(volatile u32 XP *)a;*p=*p+b;return *p;}
u32 volatile16(u32 a,u32 b) ABI {volatile u16 XP *p=(volatile u16 XP *)a;*p=(u16)(*p+(u16)b);return *p;}
u32 volatile8(u32 a,u32 b) ABI {volatile u8 XP *p=(volatile u8 XP *)a;*p=(u8)(*p+(u8)b);return *p;}
u32 same_alias(u32 a,u32 b) ABI {return alias32((u32 XP *)a,(u32 XP *)a);}
u32 different_alias(u32 a,u32 b) ABI {return alias32((u32 XP *)a,(u32 XP *)(a+4));}
u32 char_alias(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)a;u32 v=*p;*((u8 XP *)p+1)=(u8)b;return *p^v;}
u32 postinc(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)a;u32 s=0;u16 n=3;while(n--)s+=*p++;return s^b;}
u32 predec(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)(a+12);u32 s=*--p;s+=*--p;s+=*--p;return s^b;}
u32 pointer_return(u32 a,u32 b) ABI {return *advance((u8 XP *)a,(u16)(b&7));}
u32 pointer_swap(u32 a,u32 b) ABI {return swap32((u32 XP *)a,(u32 XP *)(a+4));}
u32 legacy_pointer(u32 a,u32 b) ABI {u8 XP *p=old_pointer((u8 XP *)a,b);return *p;}
u32 global_address(u32 a,u32 b) ABI {u8 XP *p=arena;return *(u32 XP *)(p+(b&7));}
u32 negative_step(u32 a,u32 b) ABI {u8 XP *p=(u8 XP *)(a+12);p-=b&7;return *p;}
u32 high_padding(u32 a,u32 b) ABI {return poisoned(a,b);}
u32 pointer_spill(u32 a,u32 b) ABI {u32 XP *p=(u32 XP *)a;u32 v0=p[0],v1=p[1],v2=p[2],v3=p[3],v4=p[4],v5=p[5],v6=p[6],v7=p[7],v8=p[8];u32 c=change(p,b);return c+((v0>>0)+(v1>>1)+(v2>>2)+(v3>>3)+(v4>>0)+(v5>>1)+(v6>>2)+(v7>>3)+(v8>>0))+(v0^v1^v2^v3^v4^v5^v6^v7^v8);}
typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef signed int s16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
#define XP __xdata
u32 load32(u32 XP *p,u32 b) ABI {return *p^b;}
u32 alias32(u32 XP *p,u32 XP *q) ABI {*p=0x13579bdfUL;*q^=0xabcdef01UL;return *p;}
u32 pair32(u32 XP *p,u32 XP *q) ABI {return *p+*q*3UL;}
u32 swap32(u32 XP *p,u32 XP *q) ABI {return pair32(q,p);}
u8 XP *advance(u8 XP *p,u16 n) ABI {return p+n;}
u32 ptrbits(u8 XP *p,u32 b) ABI {return (u32)p^b;}
u32 change(u32 XP *p,u32 v) ABI {*p=v;return v+1;}
