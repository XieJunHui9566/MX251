#include "variadic-call.h"
#include <stdio.h>
static const u8 __code letters[]="VARARGS251";

u32 variadic_case(u8 seed) {
    u8 local[4];
    char text[32];
    signed char small=seed-32;
    u16 word=0x8000u+seed,value,alias=0x2300u+seed,i;
    u32 wide=0x31415926UL+seed,result;
    const u8 *p;
    local[0]=seed;local[1]=seed+3;local[2]=seed+6;local[3]=seed+9;
    result=variadic_mixed(seed,small,word,wide,letters,local,1.5f);
    variadic_sample=0x6000u+seed;
    wide=variadic_samples(seed,variadic_sample,variadic_sample);
    result=(result<<5)^result^wide;
    result=(result<<5)^result^variadic_sample;
    value=variadic_many(15,word,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u);
    result=(result<<5)^result^value;
    value=variadic_mutate(&alias,word);
    result=(result<<5)^result^value;
    result=(result<<5)^result^alias;
    value=variadic_empty(word);
    result=(result<<5)^result^value;
    value=variadic_packed(seed,(u8)(seed+7),(u16)(0x4567u+seed));
    result=(result<<5)^result^value;
    p=variadic_pointer(seed,letters);
    result=(result<<5)^result^p[seed&7];
    value=sprintf(text,"%02x:%u:%s",(u16)(seed+16),(u16)(seed+64),"V");
    result=(result<<5)^result^value;
    for(i=0;i<=value;++i)result=(result<<5)^result^(u8)text[i];
    return result;
}

float variadic_float_caller(u8 tag) { return variadic_float(tag,-2.5f); }
