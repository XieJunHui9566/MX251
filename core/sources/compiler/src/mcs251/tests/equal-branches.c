/* Branch/call stress without device headers or SDK dependencies. */
#ifdef __SDCC
typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
#ifndef TEST_ABI
#define TEST_ABI 2
#endif
#define ABI __reentrant __sdcccall(TEST_ABI)
#else
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define ABI
#endif
typedef u32 (*Operation)(u32,u8) ABI;
static volatile u8 flag,events;
static u8 anchor;
u32 branch_add(u32 x,u8 y) ABI {events+=1;return x+(u32)y*257UL+0x1234UL;}
u32 branch_mix(u32 x,u8 y) ABI {events+=3;return (x<<3)^(x>>7)^y;}
u32 branch_many(u32 x,u8 y,u16 z,u32 a,const u8 *p) ABI {
    events+=5;return (x+a)^((u32)z<<8)^y^*p;
}

#define LOOP(name,yes,no) \
u32 name(u8 seed) ABI {u8 i;u32 r=0xabcdef01UL^seed; \
    Operation fn=(seed&1)?branch_add:branch_mix; \
    for(i=0;i<8;++i) {if(i&2) {yes;} else {no;}} return r;}

LOOP(equal_indirect,r=(*fn)(r,(u8)(seed+i)),r=fn(r,(u8)(seed+i)))
LOOP(equal_direct,r=branch_add(r,(u8)(seed+i)),r=branch_add(r,(u8)(seed+i)))
LOOP(equal_many,r=branch_many(r,(u8)(seed+i),(u16)(seed+0x8100U),r^seed,&anchor),
                r=branch_many(r,(u8)(seed+i),(u16)(seed+0x8100U),r^seed,&anchor))
LOOP(equal_chain,r=fn(r,(u8)(seed+i));r=branch_mix(r,i),
                 r=fn(r,(u8)(seed+i));r=branch_mix(r,i))
LOOP(equal_samples,r=fn(r,flag),r=fn(r,flag))
LOOP(equal_store,flag=i;r=fn(r,flag),flag=i;r=fn(r,flag))
LOOP(different_target,r=branch_add(r,i),r=branch_mix(r,i))
LOOP(different_argument,r=fn(r,(u8)(seed+i)),r=fn(r,(u8)(seed+i+1)))
LOOP(different_order,flag=i;r=fn(r,flag),r=fn(r,flag);flag=i)
LOOP(different_reads,r=fn(r,flag),flag;r=fn(r,flag))
LOOP(different_pointer,r=branch_many(r,i,0x1234,r,&anchor),
                       r=branch_many(r,i,0x1234,r,&i))

u32 equal_condition(u8 seed) ABI {
    u8 i;u32 r=seed;Operation fn=(seed&1)?branch_add:branch_mix;
    for(i=0;i<8;++i) {flag=seed+i;if(flag&2) r=fn(r,i);else r=fn(r,i);}
    return r;
}
u32 different_result(u8 seed) ABI {
    u8 i;u32 r=seed,s=0x98765432UL;Operation fn=(seed&1)?branch_add:branch_mix;
    for(i=0;i<8;++i) {if(i&2)r=fn(r,i);else s=fn(r,i);r^=s;}
    return r;
}
u32 external_entry(u8 seed) ABI {
    u8 i;u32 r=seed;Operation fn=(seed&1)?branch_add:branch_mix;
    for(i=0;i<8;++i) {
        if(seed&4)goto second;
        if(i&2)r=fn(r,(u8)(seed+i));
        else {second:r=fn(r,(u8)(seed+i));}
    }
    return r;
}
u32 switch_entry(u8 seed) ABI {
    u8 i;u32 r=seed;Operation fn=(seed&1)?branch_add:branch_mix;
    for(i=0;i<8;++i) {
        switch((seed+i)&7) {
        case 0:r+=3;break;case 1:r^=7;break;case 2:r+=11;break;
        case 3:r^=13;break;case 4:goto second;case 5:r+=17;break;default:r^=19;}
        if(i&2)r=fn(r,(u8)(seed+i));else {second:r=fn(r,(u8)(seed+i));}
    }
    return r;
}
u32 equal_nested(u8 seed) ABI {
    u8 i;u32 r=seed;Operation fn=(seed&1)?branch_add:branch_mix;
    for(i=0;i<8;++i) {
        if(i&2) {if(i&4)r=fn(r,i);else r=fn(r,i);}
        else {if(i&1)r=fn(r,i);else r=fn(r,i);}
    }
    return r;
}
u32 equal_arithmetic(u8 seed) ABI {
    u8 i;u32 r=0xabcdef01UL^seed;
    for(i=0;i<8;++i) {
        if(i&2)r=((r<<3)^(r>>7))+(u8)(seed+i);
        else r=((r<<3)^(r>>7))+(u8)(seed+i);
    }
    return r;
}
u32 different_literals(u8 seed) ABI {
    u8 i;u32 r=seed;Operation fn=(seed&1)?branch_add:branch_mix;
    for(i=0;i<8;++i) {if(i&2)r=fn(r,0x31);else r=fn(r,0x32);}
    return r;
}
u32 stress_run(u8 kind,u8 seed) {
    u32 total=0,result;u8 j;
    for(j=0;j<4;++j) {
        u8 input=(u8)(seed+64*j);events=0;flag=input;anchor=input^0x5a;
        switch(kind) {
        case 0:result=equal_indirect(input);break;
        case 1:result=equal_direct(input);break;
        case 2:result=equal_many(input);break;
        case 3:result=equal_chain(input);break;
        case 4:result=equal_samples(input);break;
        case 5:result=equal_store(input);break;
        case 6:result=different_target(input);break;
        case 7:result=different_argument(input);break;
        case 8:result=different_order(input);break;
        case 9:result=different_reads(input);break;
        case 10:result=different_pointer(input);break;
        case 11:result=equal_condition(input);break;
        case 12:result=different_result(input);break;
        case 13:result=external_entry(input);break;
        case 14:result=switch_entry(input);break;
        case 15:result=equal_nested(input);break;
        case 16:result=equal_arithmetic(input);break;
        default:result=different_literals(input);break;
        }
        total=(total<<3)^(total>>29)^result^((u32)events<<16)^((u32)flag<<24);
    }
    return total;
}
