#include "wide-ordinary.h"
u32 zero(void) OLD {return 0xdeadbeefUL;}
s8 byte_value(s8 a,u32 b) OLD {return (s8)((u8)a^(u8)b);}
s16 word_value(s16 a,u8 b) OLD {return (s16)((u16)a+((u16)b<<8));}
u32 dword_value(u32 a,u32 b) OLD {return (a^b)+0x87654321UL;}
u8 PTR *pointer_value(u8 PTR *a,u32 b,u8 PTR *c) OLD {return (b&1U)?a:c;}
float float_value(float a,float b,u8 choose) OLD {return choose?a:b;}
u32 many(u8 a,u16 b,u32 c,s8 d,u8 PTR *e,s16 f,u32 g,u8 h) OLD
{return a+((u32)b<<8)+c+(u32)d+*e+(u32)f+g+((u32)h<<24);}
void store_value(u32 PTR *p,u32 a,u32 b) OLD {*p=(a+b)^0x12345678UL;}
u32 chain(u32 a,u32 b) OLD {return native_leaf(b,a)+a;}
