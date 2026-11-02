typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
typedef union {u32 u;float f;} bits;
#define ABI __reentrant __sdcccall(4)
extern volatile __data u8 g8;
extern volatile __data s8 gs8;
extern volatile __data u16 g16;
extern volatile __data s16 gs16;
extern volatile __data u32 g32;
extern volatile __data float gf;
extern __data u32 plain32;
u32 old_mutate(u32,u32) ABI;
u32 old_return(u32,u32) ABI;

u32 gs_bytes(u32 x,u32 n) ABI {
  g8=(u8)x;g8^=(u8)n;g16=g8;g32=g16;return g32+g8;
}
u32 gs_words(u32 x,u32 n) ABI {
  g16=(u16)x;g16+=(u16)n;g32=g16;return g32^((u32)g16<<16);
}
u32 gs_sequence(u32 x,u32 n) ABI {
  u32 a,b;g32=x;a=g32;g32=x^n;b=g32;g32=a+b;return g32^(g32>>8);
}
u32 gs_loop(u32 x,u32 n) ABI {
  u8 i;g32=x;for(i=0;i<8;++i)g32+=(u8)n+i;return g32;
}
u32 gs_signed(u32 x,u32 n) ABI {
  gs8=(s8)x;gs16=(s16)n;return (u32)((s32)gs8+(s32)gs16);
}
u32 gs_call(u32 x,u32 n) ABI {
  u32 a;g32=x;g16=(u16)n;a=old_mutate(n,x);return a+g32+g16;
}
u32 gs_call_result(u32 x,u32 n) ABI {
  g32=old_return(x,n);return g32^(g32<<3);
}
u32 gs_float(u32 x,u32 n) ABI {
  bits a,b;a.u=x;gf=a.f;b.f=gf;return b.u^n;
}
u32 gs_plain(u32 x,u32 n) ABI {
  plain32=x;plain32+=n;return plain32^(plain32>>7);
}
u32 gs_recursive(u32 x,u32 depth) ABI {
  u32 a;g32+=x;a=g32;
  if(depth)a+=gs_recursive(x+3,depth-1);
  return a+g32;
}
u32 gs_rec(u32 x,u32 n) ABI {g32=0;return gs_recursive(x,n&3);}
