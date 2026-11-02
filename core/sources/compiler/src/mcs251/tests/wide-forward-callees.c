#include "wide-forward.h"
s8 ret_byte(s8 a,u32 b) OLD {return (s8)((u8)a^(u8)b);}
s16 ret_word(s16 a,u32 b) OLD {return (s16)((u16)a+(u16)b);}
u32 ret_dword(u32 a,u32 b) OLD {return (a^b)+0x87654321UL;}
float ret_float(float a,float b,u8 choose) OLD {return choose?a:b;}
u32 PTR *ret_pointer(u32 PTR *p,u32 PTR *q,u8 choose) OLD {return choose?p:q;}
u32 touch(u32 PTR *p,u32 v) OLD {*p+=v;return *p^0x55aa55aaUL;}
u32 touch_volatile(volatile u32 PTR *p,u32 v) OLD {*p^=v;return *p;}
u32 ret_many(u8 a,u16 b,u32 c,s8 d,u8 PTR *e,s16 f,u32 g,u8 h) OLD
{return a+((u32)b<<8)+c+(u32)d+*e+(u32)f+g+((u32)h<<24);}
u32 native_ret(u32 a,u32 b) ABI {return a+b;}
