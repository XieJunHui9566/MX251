#include "wide-ordinary-calls.h"
u8 read_byte(u8 a,u8 b) OLD {return a^b;}
s16 read_word(s16 a,s16 b) OLD {return a^b;}
u32 read_long(u32 a,u32 b) OLD {return (a+b)^a;}
float read_float(float a) OLD {return a;}
u8 PTR *read_pointer(u8 PTR *p,u8 n) OLD {return p+n;}
u32 across_calls(u32 a,u32 b) OLD {u32 c=old_pair(b,a);return old_pair(c,b)^a;}
u32 across_branch(u32 a,u32 b) OLD {return b&1U?old_pair(b,a)+a:old_pair(a,b)^a;}
u32 across_loop(u32 a,u8 n) OLD {u32 b=0;while(n--)b=old_pair(b,a);return b^a;}
u32 neighboring_object(u32 a,u32 b) OLD
{volatile u8 guard[3];guard[0]=(u8)a;guard[1]=(u8)b;guard[2]=0xa5U;return old_pair(a,b)+guard[0]+guard[1]+guard[2]+a;}
u32 mutable_input(u32 a,u32 b) OLD {while(b&3U){a+=b;b--;}return a;}
u32 addressed_input(u32 a,u32 b) OLD {mutate(&a,&a,b);return a;}
