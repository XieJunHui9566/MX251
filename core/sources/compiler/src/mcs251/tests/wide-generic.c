typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u8 *g_advance(u8 *,u32) ABI;
u32 g_mutate(u32 *,u32) ABI;
u32 g_load(const u32 *p,u32 x) ABI {return *p^x;}
u32 g_store(u32 *p,u32 x) ABI {*p=x;return *p;}
u16 g_word(const u16 *p) ABI {return *p;}
signed long g_signed(const s8 *p) ABI {return *p;}
float g_float(const float *p) ABI {return *p;}
float g_float_store(float *p,float x) ABI {*p=x;return *p;}
float g_volatile(volatile float *p,float x) ABI {*p=x;return *p;}
float g_vbits(u32 address,float x) ABI {volatile float *p=(volatile float *)address;*p=x;return *p;}
u8 *g_advance(u8 *p,u32 x) ABI {return p+x;}
u32 g_ptrbits(u8 *p,u32 x) ABI {return (u32)p^x;}
u8 g_chain(u8 *p,u32 x) ABI {return *g_advance(p,x);}
u32 g_alias(u32 *p,u32 *q) ABI {*p=0x13579bdfUL;*q^=0xabcdef01UL;return *p;}
u32 g_mutate(u32 *p,u32 x) ABI {*p=x;return x+1;}
u32 g_spill(u32 *p,u32 x) ABI {
    u32 a=p[0],b=p[1],c=p[2],d=p[3],e=p[4],f=p[5],g=p[6],h=p[7],i=p[8];
    u32 v=g_mutate(p,x);
    return v+(a+(b>>1)+(c>>2)+(d>>3)+e+(f>>1)+(g>>2)+(h>>3)+i)+(a^b^c^d^e^f^g^h^i);
}
/* 代码区地址通过原有 24 位平坦重定位读取，不丢高字节。 */
extern const __code u8 code_bytes[4];
u8 g_code(u32 x) ABI {const u8 *p=code_bytes;return p[x&3];}
