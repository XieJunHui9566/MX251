#include "symbol-address.h"
static const u8 __code alpha[]="Symbol-address-251";
static const u8 __code beta[]="STACK-register-CODE";
static u8 __xdata ram[4];
u32 address_case(u8 seed) {
    const u8 *table[4]={alpha,beta,alpha+3,beta+4};
    const u8 *p;
    u32 result=0x13579bdfUL;
    u16 value;
    u16 i;
    ram[0]=seed;ram[1]=seed+13;ram[2]=seed+26;ram[3]=seed+39;
    value=address_first(alpha,8,seed);
    result=(result<<5)^result^value;
    value=address_stack(seed+29,beta,6,alpha+3);
    result=(result<<5)^result^value;
    for(i=0;i<4;++i) {
        value=address_first(table[(seed+i)&3],5,seed+i);
        result=(result<<5)^result^value;
    }
    if(seed&1)p=alpha;else p=beta;
    for(i=0;i<4;++i) {
        value=address_first(p,3,seed+i);++p;
        result=(result<<5)^result^value;
    }
    value=address_code((const u8 __code *)p,3,seed+71);
    result=(result<<5)^result^value;
    value=address_first(ram,4,seed+101);
    return (result<<5)^result^value;
}
