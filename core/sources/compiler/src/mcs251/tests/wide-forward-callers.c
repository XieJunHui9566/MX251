#include "wide-forward.h"
s8 f_byte(s8 a,u32 b) OLD {return ret_byte(a,b);}
s16 f_word(s16 a,u32 b) OLD {return ret_word(a,b);}
u32 f_dword(u32 a,u32 b) OLD {return ret_dword(a,b);}
float f_float(float a,float b,u8 select) OLD {return ret_float(a,b,select);}
u32 PTR *f_pointer(u32 PTR *p,u32 PTR *q,u8 select) OLD {return ret_pointer(p,q,select);}
u32 f_local(u32 a,u32 b) OLD {u32 local=a;return touch(&local,b);}
u32 f_formal(u32 a,u32 b) OLD {return touch(&a,b);}
u32 f_volatile(u32 a,u32 b) OLD {volatile u32 local=a;return touch_volatile(&local,b);}
u32 f_recur(u32 a,u32 b,u8 n) OLD
{if(!n)return a^b;return f_recur(b,a^b,(u8)(n-1U));}
u32 f_many(u32 a,u32 b) OLD
{struct {u8 v;} local;local.v=(u8)(a^b);return ret_many((u8)a,(u16)b,a,(s8)b,&local.v,(s16)a,b,(u8)(a>>24));}
u32 c_pair(u32 a,u32 b) OLD {return (u32)f_byte((s8)a,b);}
u32 c_native(u32 a,u32 b) OLD {return (u32)f_word((s16)a,b);}
u32 c_recur(u32 a,u32 b) OLD {return f_recur(a,b,(u8)(a&3U));}
u32 c_alias(u32 a,u32 b) OLD {return f_local(a,b);}
u32 c_many(u32 a,u32 b) OLD {return f_many(a,b);}
u32 c_pointer(u32 a,u32 b) OLD
{struct {u32 x,y;} local;u32 PTR *p;local.x=a;local.y=b;p=f_pointer(&local.x,&local.y,(u8)(b&1U));return *p;}
u32 c_float(u32 a,u32 b) OLD
{Bits x,y,z;x.u=a;y.u=b;z.f=f_float(x.f,y.f,(u8)(a&1U));return z.u;}
u32 c_pressure(u32 a,u32 b) OLD
{u32 x=f_dword(a,b);return x+(u32)f_byte((s8)b,a)+(u32)f_word((s16)b,a);}
u32 c_frame(u32 a,u32 b) OLD {return f_volatile(a,b);}
u32 c_callback(u32 a,u32 b) OLD {return f_formal(a,b);}
/* These boundaries must retain their actual conversion or later side effect. */
u32 negative_add(u32 a,u32 b) OLD {return ret_dword(a,b)+1UL;}
u32 negative_store(u32 a,u32 b,volatile u32 PTR *p) OLD {u32 v=ret_dword(a,b);*p=v;return v;}
u32 negative_native_call(u32 a,u32 b) OLD {return native_ret(a,b);}
u32 negative_native_entry(u32 a,u32 b) ABI {return ret_dword(a,b);}
