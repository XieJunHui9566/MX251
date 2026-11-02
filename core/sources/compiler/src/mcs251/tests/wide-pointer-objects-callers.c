#include "wide-pointer-objects.h"
u32 c_local(u32 a,u32 b) ABI {
 u32 x=a,y=b;P32 p=&x;u32 z=swap_pointer(&p,&y,a^b);
 *p^=a;return z+x+y;
}
u32 first_pointer(P32 p,P32 q,u32 salt) ABI {
 u32 z=swap_pointer(&p,q,salt);*p+=salt;return z^*p;
}
u32 c_first(u32 a,u32 b) ABI {u32 x=a,y=b;u32 z=first_pointer(&x,&y,a^b);return z+x+y;}
u32 second_pointer(P32 p,P32 q,u32 salt) ABI {
 u32 z=swap_pointer(&q,p,salt);*q^=salt;return z+*q;
}
u32 c_second(u32 a,u32 b) ABI {u32 x=a,y=b;u32 z=second_pointer(&x,&y,a+b);return z^x^y;}
u32 third_pointer(u32 a,u32 b,P32 p,P32 q,u8 guard) ABI {
 u32 z=swap_pointer(&p,q,a);*p^=b;return z+*p+guard;
}
u32 c_third(u32 a,u32 b) ABI {u32 x=a,y=b;u32 z=third_pointer(a,b,&x,&y,(u8)(a^b));return z+x+y;}
u32 mixed_pointer(u8 a,u16 b,P32 p,u8 d,u16 e,P32 q,u8 g,u32 h) ABI {
 u32 z=swap_pointer(&p,q,h);*p+=b;z^=read_pointer(&p);
 return z+a+b+d+e+g+h;
}
u32 c_mixed(u32 a,u32 b) ABI {u32 x=a,y=b;return mixed_pointer((u8)a,(u16)b,&x,(u8)(a>>8),(u16)(b>>16),&y,(u8)(a^b),a+b)^x^y;}
u32 c_alias(u32 a,u32 b) ABI {
 u32 x=a,y=b;P32 p=&x,q=&y;P32 PTR *pp=(b&1)?&p:&q;
 *pp=&x;**pp+=b;return read_pointer(&p)^read_pointer(&q)^x^y;
}
u32 c_volatile(u32 a,u32 b) ABI {
 u32 x=a,y=b;P32 volatile p=&x;P32 volatile PTR *pp=&p;
 u32 z=**pp;*pp=&y;**pp^=a;return z+*p+y;
}
u32 c_loop(u32 a,u32 b) ABI {
 u32 x=a,y=b,z=0;P32 p=&x;u8 i;
 for(i=0;i<(u8)(b&7);++i){z+=swap_pointer(&p,(i&1)?&x:&y,a+i);*p^=b+i;}
 return z^x^y^*p;
}
u32 c_bits(u32 a,u32 b) ABI {
 Guard g;P32 p;u32 z;
 g.before=(u8)a;g.after=(u8)b;g.pointer=(P32)(UPTR)(a&0xffffffUL);
 p=g.pointer;z=(u32)(UPTR)p;set_bits(&g.pointer,b);
 p=g.pointer;return z^(((u32)(UPTR)p)+((u32)g.before<<24)+((u32)g.after<<16));
}
u32 c_pressure(u32 a,u32 b) ABI {
 u32 x=a,y=b,t0=a+1,t1=a+0x12345678UL,t2=b^0xa5a5a5a5UL,t3=b+0x10001UL;
 u32 t4=a^0xdeadbeefUL,t5=a+b,t6=a-b,t7=b+0x87654321UL;
 P32 p=&x;u32 z=swap_pointer(&p,&y,a^b);*p+=a;
 return z^(*p+t0+t1+t2+t3+t4+t5+t6+t7);
}
