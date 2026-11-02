#include "wide-parameter-write.h"
u32 legacy_value(u32 a,u32 b) OLD {return (a^b)+0x87654321UL;}
u32 c_byte(u32 a,u32 b) ABI {return p_byte(a,b,(u8)a,b);}
u32 c_word(u32 a,u32 b) ABI {return p_word(a,b,(s16)a,(u8)(a>>8));}
u32 c_dword(u32 a,u32 b) ABI {return p_dword(a,b,(u8)a,a^b,(u16)b);}
u32 c_pointer(u32 a,u32 b) ABI
{struct {u8 x,y,z;} v;u32 x;v.x=(u8)a;v.y=(u8)b;v.z=(u8)(a^b);x=p_pointer(a,b,0x3b,&v.x,0xa65c,&v.y);
 return x^p_address(a,b,0x3b,(u8 PTR *)(uaddr)(a&0xffffffUL),0xa65c,(u8 PTR *)(uaddr)(b&0xffffffUL),a^b,0xc7);}
u32 c_float(u32 a,u32 b) ABI
{Bits x,y,z;x.u=a;y.u=b;z.f=p_float(a,b,0x3b,x.f,0xa65c,y.f);return z.u;}
u32 c_eight(u32 a,u32 b) ABI {return p_eight(a,b,(u8)a,(u16)b,a^b,(u8)(a>>8),(u16)(b>>16),a+b);}
u32 c_loop(u32 a,u32 b) ABI {return p_loop(a,b,(u8)(a&7U),a^b,(u16)b);}
u32 c_recurse(u32 a,u32 b) ABI {return p_recurse(a,b,(u8)(a&3U),a^b,0xa65c);}
u32 c_frame(u32 a,u32 b) ABI {return p_frame(a,b,0x3b,a^b,0xa65c);}
u32 c_pressure(u32 a,u32 b) ABI {return p_pressure(a,b,0x3b,a^b,0xa65c);}
