/* Supplemental replacement for c_float: local aliasing across real calls,
 * volatile bit transport, and adjacent guards must remain observable. */
u32 c_float(u32 a,u32 b) OLD
{
 struct {u32 before;Bits value;u32 after;} x;
 volatile Bits y;u32 t;
 x.before=0x13579bdfUL;x.value.u=a;x.after=0x2468ace0UL;y.u=b;
 t=mutate(&x.value.u,&x.after,b);
 x.value.f=forward_float(x.value.f,y.f,(u8)(a&1U));
 return x.value.u+x.before+(x.after^t)+y.u;
}
