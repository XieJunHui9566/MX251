#include "value-storage.h"

static void vs_swap(u16 *a,u16 *b)
{u16 t=*a;*a=*b;*b=t;}

u32 vs_buffers(u8 seed)
{
    u8 zero[31]={0};
    u8 pattern[9]={0x12,0x34,0x80,0xff,0,0xa5,0x5a,0xfe,0x71};
    u32 result=vs_visit(zero,31,seed);
    result^=vs_three(seed,pattern+1,zero+3,pattern+5,0xa75b);
    result^=vs_variadic(seed,pattern+2,(u16)0x9137,zero+19);
    result^=vs_visit(pattern,9,seed+17);
    return result^vs_visit(zero,31,seed+5);
}

u32 vs_values(u8 seed)
{
    u16 a=0xff80U+seed,b=0x8001U^seed,c=17;
    u8 i;
    u32 result=0x2468ace0UL;
    for(i=0;i<6;++i) {
        if((a^b)&1)vs_swap(&a,&b);
        switch((seed+i)&3) {
        case 0:c=a;b+=c;break;
        case 1:c=b;a^=c;break;
        case 2:c=a+b;a-=7;break;
        default:c=a-b;b^=0x8123;break;
        }
        result=(result<<3)^result^((u32)c<<16)^a^b;
    }
    c=a;a=b;b=c;
    return result^((u32)a<<16)^b;
}

u32 vs_recursive(u8 seed,u8 depth)
{
    u8 data[7]={3,0xf1,0x82,7,0,0xff,0x56};
    u32 result=vs_visit(data,7,seed);
    if(depth)result^=vs_recursive(seed+3,depth-1);
    return result^vs_visit(data+1,5,seed+7);
}

u32 vs_volatile(u8 seed)
{
    volatile u8 data[5]={1,3,5,7,9};
    data[seed%5]^=seed;
    return ((u32)data[0]<<24)^((u32)data[1]<<16)^((u16)data[2]<<8)^data[3]^data[4];
}

u32 vs_case(u8 seed)
{return vs_buffers(seed)^vs_values(seed)^vs_recursive(seed,seed&3)^vs_volatile(seed);}
