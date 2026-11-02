#include "wide-stack-pointers.h"
u32 call_pattern(u32 a,u32 b) ABI {
  return ptr8(a,b,(u8)a,(u8 X *)a,(u16)b,(u32 X *)b,(s8)(a>>8),(u8 X *)(a^b))+a+b;
}
u32 call_literal(u32 a,u32 b) ABI {
  return ptr8(a,b,0x69,(u8 X *)0xffffffUL,0x9696,(u32 X *)0x800000UL,-91,(u8 X *)0x123456UL)^a;
}
u32 call_pressure(u32 a,u32 b) ABI {
  u8 X *p0=(u8 X *)(a+0x012345UL),*p1=(u8 X *)(a+0x123456UL),*p2=(u8 X *)(a+0x234567UL),*p3=(u8 X *)(a+0x345678UL);
  u8 X *p4=(u8 X *)(a+0x456789UL),*p5=(u8 X *)(a+0x56789aUL),*p6=(u8 X *)(a+0x6789abUL),*p7=(u8 X *)(a+0x789abcUL);
  u32 result=many_ptr(p7,p6,p5,p4,p3,p2,p1,p0);
  return result+(u32)p0+(u32)p1+(u32)p2+(u32)p3+(u32)p4+(u32)p5+(u32)p6+(u32)p7+b;
}
u32 call_memory(u32 a,u32 b) ABI {
  struct {u32 guard;u8 bytes[10];u32 tail;} local;
  u32 result;u16 index=(u16)(a&7UL);
  local.guard=0xa55a369cUL;local.tail=0x5aa5c963UL;
  local.bytes[index]=(u8)a;local.bytes[index+1]=(u8)b;
  result=change3(a,b,local.bytes,index,a^b);
  result^=generic3(a,b,local.bytes,index+1);
  result+=volatile3(a,b,local.bytes+index,0x12345678UL);
  result^=*retptr(a,b,local.bytes,index);
  return result^local.guard^local.tail;
}
u32 call_recursive(u32 a,u32 b) ABI {
  u8 bytes[2];bytes[0]=(u8)a;bytes[1]=(u8)b;
  return recurseptr(a,b&7UL,bytes,bytes+1)+bytes[0]+bytes[1];
}
u32 call_narrow(u32 a,u32 b) ABI {return narrowptr(a,b,(u8 X *)(a^b))^ptr3(b,a,(u8 X *)a);}
