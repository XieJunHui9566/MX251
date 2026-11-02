#include "wide-ordinary-calls.h"
u8 byte_home(u8 a,u8 b) OLD {a^=b;return a+3U;}
s16 word_home(s16 a,s16 b) OLD {a^=b;return a;}
u32 long_home(u32 a,u32 b) OLD {a+=b;return a^0x13579bdfUL;}
float float_home(float a) OLD {return a;}
u8 PTR *pointer_home(u8 PTR *p,u8 step) OLD {p+=step;return p;}
u32 call_home(u32 a,u32 b) OLD {u32 c=old_pair(b,a);return c+a+b;}
u32 alias_home(u32 a,u32 b) OLD {mutate(&a,&a,b);return a;}
u32 loop_home(u32 a,u8 count) OLD {while(count--)a=(a<<1)^0x1021UL;return a;}
u32 qualified_pointee(volatile u32 PTR *p,u32 a) OLD {*p=a;return *p+*p;}
u32 unused_home(u32 a,u32 b) OLD {(void)a;return b;}
